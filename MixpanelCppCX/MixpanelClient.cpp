#include "pch.h"
#include "EventStorageQueue.h"
#include "MixpanelClient.h"
#include "PayloadEncoder.h"

using namespace Codevoid::Utilities::Mixpanel;
using namespace concurrency;
using namespace Platform;
using namespace Platform::Collections;
using namespace std;
using namespace std::chrono;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Security::Cryptography;
using namespace Windows::Security::Cryptography::Core;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;
using namespace Windows::Web::Http;
using namespace Windows::Web::Http::Headers;

constexpr wchar_t MIXPANEL_TRACK_BASE_URL[] = L"https://api.mixpanel.com/track";

constexpr int WINDOWS_TICK = 10000000;
constexpr long long SEC_TO_UNIX_EPOCH = 11644473600LL;
constexpr auto SUPER_PROPERTIES_CONTAINER_NAME = L"Codevoid_Utilities_Mixpanel";
constexpr auto MIXPANEL_QUEUE_FOLDER = L"MixpanelUploadQueue";
constexpr auto CRYPTO_TOKEN_HMAC_NAME = L"SHA256";
constexpr auto DURATION_PROPERTY_NAME = L"duration";
constexpr auto SESSION_TRACKING_EVENT = L"Session";

constexpr vector<shared_ptr<PayloadContainer>>::difference_type DEFAULT_UPLOAD_SIZE_STRIDE = 50;

// Sourced from:
// http://stackoverflow.com/questions/6161776/convert-windows-filetime-to-second-in-unix-linux
unsigned Codevoid::Utilities::Mixpanel::WindowsTickToUnixSeconds(const long long windowsTicks)
{
    return (unsigned)(windowsTicks / WINDOWS_TICK - SEC_TO_UNIX_EPOCH);
}

String^ HashTokenForSettingContainerName(String^ token)
{
    // Convert the message string to binary data.
    auto inputBuffer = CryptographicBuffer::ConvertStringToBinary(token, BinaryStringEncoding::Utf8);

    // Create a HashAlgorithmProvider object.
    auto hashAlgorithm = HashAlgorithmProvider::OpenAlgorithm(StringReference(CRYPTO_TOKEN_HMAC_NAME));

    // Hash the message.
    auto hashedBuffer = hashAlgorithm->HashData(inputBuffer);

    // Convert the hash to a string (for display).
    return CryptographicBuffer::EncodeToBase64String(hashedBuffer);
}

IPropertySet^ CopyOrCreatePropertySet(IPropertySet^ properties)
{
    IPropertySet^ result = ref new PropertySet();
    if (properties == nullptr)
    {
        return result;
    }

    for (const auto& kvp : properties)
    {
        result->Insert(kvp->Key, kvp->Value);
    }

    return result;
}

void MergePropertySet(IPropertySet^ destination, IPropertySet^ source)
{
    if (source == nullptr)
    {
        return;
    }

    for (const auto& kvp : source)
    {
        destination->Insert(kvp->Key, kvp->Value);
    }
}

MixpanelClient::MixpanelClient(String^ token) :
    m_userAgent(ref new HttpProductInfoHeaderValue(L"Codevoid.Utilities.MixpanelClient", L"1.0")),
    m_uploadWorker(
        [this](const auto& items, const auto& shouldContinueProcessing) -> auto {
            // Not using std::bind, because ref classes & it don't play nice
            return this->HandleEventBatchUpload(items, shouldContinueProcessing);
        },
        [this](const auto& items) -> void {
            this->HandleCompletedUploads(items);
        },
        wstring(L"UploadToMixpanel")
    )
{
    if (token->IsEmpty())
    {
        throw ref new InvalidArgumentException(L"Must provide a token for sending data");
    }

    m_token = token;
    this->PersistSuperPropertiesToApplicationData = true;
    this->AutomaticallyAttachTimeToEvents = true;
    this->AutomaticallyTrackSessions = true;
}

IAsyncAction^ MixpanelClient::InitializeAsync()
{
    return create_async([this]() {
        this->Initialize().wait();
    });
}

void MixpanelClient::StartWorker()
{
    m_uploadWorker.Start();
    m_eventStorageQueue->EnableQueuingToStorage();
}

void MixpanelClient::Start()
{
    this->StartWorker();
    this->StartSessionTracking();
    m_suspendingEventToken = CoreApplication::Suspending += ref new EventHandler<SuspendingEventArgs^>(this, &MixpanelClient::HandleApplicationSuspend);
    m_resumingEventToken = CoreApplication::Resuming += ref new EventHandler<Object^>(this, &MixpanelClient::HandleApplicationResuming);
    m_enteredBackgroundEventToken = CoreApplication::EnteredBackground += ref new EventHandler<EnteredBackgroundEventArgs^>(this, &MixpanelClient::HandleApplicationEnteredBackground);
    m_leavingBackgroundEventToken = CoreApplication::LeavingBackground += ref new EventHandler<LeavingBackgroundEventArgs^>(this, &MixpanelClient::HandleApplicationLeavingBackground);
}

void MixpanelClient::HandleApplicationSuspend(Object^, SuspendingEventArgs^ args)
{
    auto deferral = args->SuspendingOperation->GetDeferral();

    this->EndSessionTracking();

    this->PauseWorker().then([deferral]() {
        deferral->Complete();
    });
}

void MixpanelClient::HandleApplicationResuming(Object^, Object^)
{
    this->StartWorker();
    this->StartSessionTracking();
}

void MixpanelClient::HandleApplicationEnteredBackground(Platform::Object^, EnteredBackgroundEventArgs^)
{
    m_durationTracker.PauseTimers();
}

void MixpanelClient::HandleApplicationLeavingBackground(Object^, LeavingBackgroundEventArgs^)
{
    m_durationTracker.ResumeTimers();
}

void MixpanelClient::StartSessionTracking()
{
    if (!this->AutomaticallyTrackSessions)
    {
        return;
    }

    m_sessionTrackingStarted = true;
    this->ThrowIfNotInitialized();
    this->StartTimedEvent(StringReference(SESSION_TRACKING_EVENT));
}

void MixpanelClient::EndSessionTracking()
{
    if (!m_sessionTrackingStarted || !this->AutomaticallyTrackSessions)
    {
        return;
    }

    this->ThrowIfNotInitialized();
    this->Track(StringReference(SESSION_TRACKING_EVENT), m_sessionProperties);
    this->ClearSessionProperties();
}

void MixpanelClient::RestartSessionTracking()
{
    this->EndSessionTracking();
    this->StartSessionTracking();
}

task<void> MixpanelClient::PauseWorker()
{
    m_uploadWorker.Pause();
    return m_eventStorageQueue->PersistAllQueuedItemsToStorageAndShutdown();
}

IAsyncAction^ MixpanelClient::PauseAsync()
{
    return create_async([this]() {
        this->PauseWorker().wait();
    });
}

task<void> MixpanelClient::Shutdown()
{
    // The upload queue can be stuck in long running
    // operations, so we dont want it to drain or reach
    // a 'safe' place. We want it to give up as soon as
    // we're trying to get out of dodge.
    m_uploadWorker.ShutdownAndDrop();

    // Remove the suspending/resuming events, since we're
    // shutting everythign down. Of note, these can be detatched
    // more than once for safety.
    if (m_suspendingEventToken.Value != 0 || m_resumingEventToken.Value != 0)
    {
        CoreApplication::Suspending -= m_suspendingEventToken;
        CoreApplication::Resuming -= m_resumingEventToken;
        m_suspendingEventToken = { 0 };
        m_resumingEventToken = { 0 };
    }

    if (m_eventStorageQueue == nullptr)
    {
        return;
    }

    this->EndSessionTracking();

    co_await m_eventStorageQueue->PersistAllQueuedItemsToStorageAndShutdown();
    m_eventStorageQueue = nullptr;
}

IAsyncAction^ MixpanelClient::ClearStorageAsync()
{
    this->ThrowIfNotInitialized();
    return create_async([this]() {
        return m_eventStorageQueue->Clear();
    });
}

task<void> MixpanelClient::Initialize()
{
    auto folder = co_await ApplicationData::Current->LocalFolder->CreateFolderAsync(
        StringReference(MIXPANEL_QUEUE_FOLDER),
        CreationCollisionOption::OpenIfExists);

    this->Initialize(folder, ref new Uri(StringReference(MIXPANEL_TRACK_BASE_URL)));
    auto previousItems = co_await EventStorageQueue::LoadItemsFromStorage(folder);
    if (previousItems.size() > 0)
    {
        this->AddItemsToUploadQueue(previousItems);
    }
}

void MixpanelClient::Initialize(StorageFolder^ queueFolder,
                                Uri^ serviceUri)
{
    m_eventStorageQueue = make_unique<EventStorageQueue>(queueFolder, [this](auto writtenItems) {
        if (m_writtenToStorageMockCallback == nullptr)
        {
            this->AddItemsToUploadQueue(writtenItems);
            return;
        }

        m_writtenToStorageMockCallback(writtenItems);
    });

    m_serviceUri = serviceUri;
    m_requestHelper = &MixpanelClient::SendRequestToService;
}

void MixpanelClient::Track(String^ name, IPropertySet^ properties)
{
    this->ThrowIfNotInitialized();

    if (this->DropEventsForPrivacy)
    {
        return;
    }

    if (name->IsEmpty())
    {
        throw ref new InvalidArgumentException(L"Name cannot be empty or null");
    }

    properties = CopyOrCreatePropertySet(properties);
    this->EmbelishPropertySet(properties);
    this->AddDurationForEvent(name, properties);

    IJsonValue^ payload = this->GenerateTrackingJsonPayload(name, properties);
    m_eventStorageQueue->QueueEventToStorage(payload);
}

void MixpanelClient::StartTimedEvent(String^ name)
{
    this->ThrowIfNotInitialized();
    if (this->DropEventsForPrivacy)
    {
        return;
    }

    if (name->IsEmpty())
    {
        throw ref new InvalidArgumentException(L"Name cannot be empty or null");
    }

    m_durationTracker.StartTimerFor(name->Data());
}

void MixpanelClient::AddItemsToUploadQueue(const vector<shared_ptr<PayloadContainer>>& itemsToUpload)
{
    // If there are any normal priority items, we'll make the work we queue
    // as normal too -- but otherwise, no point in waking up the network stack
    // to process the low priority items.
    bool anyNormalPriorityItems = any_of(begin(itemsToUpload), end(itemsToUpload), [](auto item) -> bool {
        return item->Priority == EventPriority::Normal;
    });

    m_uploadWorker.AddWork(itemsToUpload, anyNormalPriorityItems ? WorkPriority::Normal : WorkPriority::Low);
}

void MixpanelClient::HandleCompletedUploads(const vector<shared_ptr<PayloadContainer>>& items)
{
    for (auto&& item : items)
    {
        m_eventStorageQueue->RemoveEventFromStorage(*item).get();
    }
}

vector<shared_ptr<PayloadContainer>> MixpanelClient::HandleEventBatchUpload(const vector<shared_ptr<PayloadContainer>>& items, const function<bool()>& /*shouldKeepProcessing*/)
{
    vector<shared_ptr<PayloadContainer>>::difference_type strideSize = DEFAULT_UPLOAD_SIZE_STRIDE;
    vector<shared_ptr<PayloadContainer>> successfulItems;
    auto front = begin(items);
    auto back = end(items);

    TRACE_OUT(L"MixpanelClient: Beginning upload of " + to_wstring(items.size()) + L" items");
    while (front != back)
    {
        auto first = front;

        // Find the last item -- capping at the end of the collection if the stride
        // size would put it past the end of the collection.
        auto totalSize = distance(front, back);
        auto last = (totalSize >= strideSize) ? (front + (strideSize - 1)) : prev(back);
        vector<IJsonValue^> eventPayload;
        eventPayload.reserve(distance(front, last)); // Pre-allocate the size.

        TRACE_OUT(L"MixpanelClient: Copying JsonValues to payload");
        while(first <= last)
        {
            eventPayload.push_back((*first)->Payload);
            first++;
        }

        TRACE_OUT(L"MixpanelClient: Sending " + to_wstring(eventPayload.size()) + L" events to service");
        if (!this->PostTrackEventsToMixpanel(eventPayload).get())
        {
            TRACE_OUT(L"MixpanelClient: Upload failed");
            if (strideSize != 1)
            {
                TRACE_OUT(L"MixpanelClient: Switching to single-event upload");
                strideSize = 1;

                // Just go around the loop again to reprocess the items
                // in the smaller stride size -- because we're using our
                // local iterator, and not updating the while iterator
                // we can just jump back to the top of the loop again
                continue;
            }
        }
        else
        {
            // These items were successfully processed, so we can now
            // put these in the list to be removed from our queue
            successfulItems.insert(end(successfulItems), front, next(last));
        }

        // Move to the beginning of the next item.
        front = first;
    }

    TRACE_OUT(L"MixpanelClient: Batch complete. " + to_wstring(successfulItems.size()) + L" items were successfully uploaded");
    return successfulItems;
}

task<bool> MixpanelClient::PostTrackEventsToMixpanel(const vector<IJsonValue^>& events)
{
    auto jsonEvents = ref new JsonArray();
    
    for (auto&& payload : events)
    {
        jsonEvents->Append(payload);
    }

    auto formPayload = ref new Map<String^, IJsonValue^>();
    formPayload->Insert(L"data", jsonEvents);

    return co_await m_requestHelper(m_serviceUri, formPayload, m_userAgent);
}

task<bool> MixpanelClient::SendRequestToService(Uri^ uri, IMap<String^, IJsonValue^>^ payload, HttpProductInfoHeaderValue^ userAgent)
{
    HttpClient^ client = ref new HttpClient();
    client->DefaultRequestHeaders->UserAgent->Append(userAgent);

    Map<String^, String^>^ encodedPayload = ref new Map<String^, String^>();

    for (auto&& pair : payload)
    {
        encodedPayload->Insert(pair->Key, EncodeJson(pair->Value));
    }

    try
    {
        auto content = ref new HttpFormUrlEncodedContent(encodedPayload);
        auto requestResult = co_await client->PostAsync(uri, content);
        auto requestBody = co_await requestResult->Content->ReadAsStringAsync();
        if (requestBody == L"0")
        {
            return false;
        }

        // IsSuccessStatusCode defines failure as 200-299 inclusive
        return requestResult->IsSuccessStatusCode;
    }
    catch (...)
    {
        return false;
    }
}

void MixpanelClient::SetUploadToServiceMock(const function<task<bool>(Uri^, IMap<String^, IJsonValue^>^, HttpProductInfoHeaderValue^)> mock)
{
    m_requestHelper = mock;
}

void MixpanelClient::SetSessionPropertyAsString(String^ name, String^ value)
{
    this->InitializeSessionPropertyCollection()->Insert(name, value);
}

void MixpanelClient::SetSessionPropertyAsInteger(String^ name, int value)
{
    this->InitializeSessionPropertyCollection()->Insert(name, value);
}

void MixpanelClient::SetSessionPropertyAsDouble(String^ name, double value)
{
    this->InitializeSessionPropertyCollection()->Insert(name, value);
}

void MixpanelClient::SetSessionPropertyAsBoolean(String^ name, bool value)
{
    this->InitializeSessionPropertyCollection()->Insert(name, value);
}

String^ MixpanelClient::GetSessionPropertyAsString(String^ name)
{
    return static_cast<String^>(this->InitializeSessionPropertyCollection()->Lookup(name));
}

int MixpanelClient::GetSessionPropertyAsInteger(String^ name)
{
    return static_cast<int>(this->InitializeSessionPropertyCollection()->Lookup(name));
}

double MixpanelClient::GetSessionPropertyAsDouble(String^ name)
{
    return static_cast<double>(this->InitializeSessionPropertyCollection()->Lookup(name));
}

bool MixpanelClient::GetSessionPropertyAsBool(String^ name)
{
    return static_cast<bool>(this->InitializeSessionPropertyCollection()->Lookup(name));
}

bool MixpanelClient::HasSessionProperty(String^ name)
{
    return this->InitializeSessionPropertyCollection()->HasKey(name);
}

void MixpanelClient::RemoveSessionProperty(String^ name)
{
    this->InitializeSessionPropertyCollection()->Remove(name);
}

IPropertySet^ MixpanelClient::InitializeSessionPropertyCollection()
{
    if (m_sessionProperties == nullptr)
    {
        m_sessionProperties = ref new ValueSet();
    }

    return m_sessionProperties;
}

void MixpanelClient::ClearSessionProperties()
{
    if (m_sessionProperties == nullptr)
    {
        return;
    }

    m_sessionProperties->Clear();
    m_sessionProperties = nullptr;
}

void MixpanelClient::SetSuperPropertyAsString(String^ name, String^ value)
{
    this->InitializeSuperPropertyCollection()->Insert(name, value);
}

void MixpanelClient::SetSuperPropertyAsInteger(String^ name, int value)
{
    this->InitializeSuperPropertyCollection()->Insert(name, value);
}

void MixpanelClient::SetSuperPropertyAsDouble(String^ name, double value)
{
    this->InitializeSuperPropertyCollection()->Insert(name, value);
}

void MixpanelClient::SetSuperPropertyAsBoolean(String^ name, bool value)
{
    this->InitializeSuperPropertyCollection()->Insert(name, value);
}

String^ MixpanelClient::GetSuperPropertyAsString(String^ name)
{
    return static_cast<String^>(this->InitializeSuperPropertyCollection()->Lookup(name));
}

int MixpanelClient::GetSuperPropertyAsInteger(String^ name)
{
    return static_cast<int>(this->InitializeSuperPropertyCollection()->Lookup(name));
}

double MixpanelClient::GetSuperPropertyAsDouble(String^ name)
{
    return static_cast<double>(this->InitializeSuperPropertyCollection()->Lookup(name));
}

bool MixpanelClient::GetSuperPropertyAsBool(String^ name)
{
    return static_cast<bool>(this->InitializeSuperPropertyCollection()->Lookup(name));
}

bool MixpanelClient::HasSuperProperty(String^ name)
{
    return this->InitializeSuperPropertyCollection()->HasKey(name);
}

void MixpanelClient::RemoveSuperProperty(String^ name)
{
    this->InitializeSuperPropertyCollection()->Remove(name);
}

IPropertySet^ MixpanelClient::InitializeSuperPropertyCollection()
{
    if (m_superProperties == nullptr)
    {
        if (this->PersistSuperPropertiesToApplicationData)
        {
            auto localSettings = ApplicationData::Current->LocalSettings;

            // Obtain the container that houses all the super properties
            // split by Token. E.g. if two instances are using different
            // tokens, they'll share different super properties.
            auto superPropertiesContainer = localSettings->CreateContainer(
                StringReference(SUPER_PROPERTIES_CONTAINER_NAME),
                ApplicationDataCreateDisposition::Always
            );

            // Create the token-specific container where we'll actually
            // store the properties.
            auto superProperties = superPropertiesContainer->CreateContainer(
                HashTokenForSettingContainerName(m_token),
                ApplicationDataCreateDisposition::Always
            );

            m_superProperties = superProperties->Values;
        }
        else
        {
            m_superProperties = ref new ValueSet();
        }
    }

    return m_superProperties;
}

void MixpanelClient::ClearSuperProperties()
{
    if (m_superProperties == nullptr)
    {
        return;
    }

    m_superProperties->Clear();
    m_superProperties = nullptr;
}

void MixpanelClient::EmbelishPropertySet(IPropertySet^ properties)
{
    MergePropertySet(properties, m_superProperties);

    // The properties payload is expected to have the API Token, rather than
    // in the general payload properties. So, lets explicitly add it.
    properties->Insert(L"token", m_token);

    if (this->AutomaticallyAttachTimeToEvents && (properties && !properties->HasKey(L"time")))
    {
        auto now = time_point_cast<milliseconds>(system_clock::now()).time_since_epoch().count();
        properties->Insert(L"time", static_cast<double>(now));
    }
}

void MixpanelClient::AddDurationForEvent(String^ name, IPropertySet^ properties)
{
    // Auto attach the duration event if there isn't already
    // an attached "duration" event.
    if (properties->HasKey(StringReference(DURATION_PROPERTY_NAME)))
    {
        return;
    }

    auto durationForEvent = m_durationTracker.EndTimerFor(name->Data());

    // If the event wasn't timed, then don't attach it.
    if (durationForEvent.has_value())
    {
        properties->Insert(
            StringReference(DURATION_PROPERTY_NAME),
            static_cast<double>((*durationForEvent).count())
        );
    }
}

JsonObject^ MixpanelClient::GenerateTrackingJsonPayload(String^ name, IPropertySet^ properties)
{
    JsonObject^ propertiesPayload = ref new JsonObject();
    MixpanelClient::AppendPropertySetToJsonPayload(properties, propertiesPayload);

    JsonObject^ trackPayload = ref new JsonObject();
    trackPayload->Insert(L"event", JsonValue::CreateStringValue(name));
    trackPayload->Insert(L"properties", propertiesPayload);

    return trackPayload;
}

void MixpanelClient::AppendPropertySetToJsonPayload(IPropertySet^ properties, JsonObject^ toAppendTo)
{
    for (const auto& kvp : properties)
    {
        // MixPanel explicilty disallows properties prefixed with mp_
        // So check each key and throw if it is unacceptable.
        wstring key(kvp->Key->Data());

        size_t prefixPos = key.find(L"mp_");
        if ((prefixPos != wstring::npos) && (prefixPos == 0))
        {
            throw ref new InvalidArgumentException(L"Arguments cannot start with mp_. Property name: " + kvp->Key);
        }

        // Work out which type this thing actually is.
        // We only support:
        // * Strings
        // * Dates
        // * Numbers (int, float, double)
        //
        // Anything else will cause a big bang, and hellfire raining from the sky.
        // Maybe, one day, this should support richer, more interesting types.
        String^ candidateString = dynamic_cast<String^>(kvp->Value);
        if (!candidateString->IsEmpty())
        {
            auto stringValue = JsonValue::CreateStringValue(candidateString);
            toAppendTo->Insert(kvp->Key, stringValue);
            continue;
        }

        IBox<bool>^ candidateBool = dynamic_cast<IBox<bool>^>(kvp->Value);
        if (candidateBool != nullptr)
        {
            auto boolValue = JsonValue::CreateBooleanValue(candidateBool->Value);
            toAppendTo->Insert(kvp->Key, boolValue);
            continue;
        }

        IBox<int>^ candidateInt = dynamic_cast<IBox<int>^>(kvp->Value);
        if (candidateInt != nullptr)
        {
            auto intValue = JsonValue::CreateNumberValue(candidateInt->Value);
            toAppendTo->Insert(kvp->Key, intValue);
            continue;
        }

        IBox<double>^ candidateDouble = dynamic_cast<IBox<double>^>(kvp->Value);
        if (candidateDouble != nullptr)
        {
            auto doubleValue = JsonValue::CreateNumberValue(candidateDouble->Value);
            toAppendTo->Insert(kvp->Key, doubleValue);
            continue;
        }

        IBox<float>^ candidateFloat = dynamic_cast<IBox<float>^>(kvp->Value);
        if (candidateFloat != nullptr)
        {
            auto floatValue = JsonValue::CreateNumberValue(candidateFloat->Value);
            toAppendTo->Insert(kvp->Key, floatValue);
            continue;
        }

        IBox<DateTime>^ candidateDateTime = dynamic_cast<IBox<DateTime>^>(kvp->Value);
        if (candidateDateTime != nullptr)
        {
            auto timeAsJsonDateTime = WindowsTickToUnixSeconds(candidateDateTime->Value.UniversalTime);
            auto dataTimeValue = JsonValue::CreateNumberValue(timeAsJsonDateTime);
            toAppendTo->Insert(kvp->Key, dataTimeValue);
            continue;
        }

        throw ref new InvalidCastException(L"Property set includes unsupported data type: " + kvp->Key);
    }
}

void MixpanelClient::ThrowIfNotInitialized()
{
    if (m_eventStorageQueue != nullptr)
    {
        return;
    }

    throw ref new InvalidArgumentException(L"Client must be initialized");
}

void MixpanelClient::SetWrittenToStorageMock(function<void(vector<shared_ptr<PayloadContainer>>)> mockCallback)
{
    m_writtenToStorageMockCallback = mockCallback;
}

void MixpanelClient::ConfigureForTesting(const milliseconds& idleTimeout, const size_t& itemThreshold)
{
    m_eventStorageQueue->DontWriteToStorageForTestPurposeses();
    m_eventStorageQueue->SetWriteToStorageIdleLimits(idleTimeout, itemThreshold);
    m_uploadWorker.SetIdleTimeout(idleTimeout);
    m_uploadWorker.SetItemThreshold(itemThreshold);
}

void MixpanelClient::ForceWritingToStorage()
{
    m_eventStorageQueue->NoReallyWriteToStorageDuringTesting();
}
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
constexpr auto DISTINCT_ID_PROPERTY_NAME = L"distinct_id";
constexpr auto DISTINCT_ID_PROPERTY_NAME_ENGAGE = L"$distinct_id";
constexpr auto TOKEN_PROPERTY_NAME_ENGAGE = L"$token";

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

String^ GenerateGuidAsString()
{
    GUID newGuid;
    wchar_t guidAsString[MAX_PATH];
    CoCreateGuid(&newGuid);
    StringFromGUID2(newGuid, guidAsString, ARRAYSIZE(guidAsString));

    return ref new String(guidAsString);
}

void ThrowIfPrefixedWithMp(String^ keyToCheck)
{
    // MixPanel explicilty disallows properties prefixed with mp_
    // So check each key and throw if it is unacceptable.
    wstring key(keyToCheck->Data());

    size_t prefixPos = key.find(L"mp_");
    if ((prefixPos != wstring::npos) && (prefixPos == 0))
    {
        throw ref new InvalidArgumentException(L"Arguments cannot start with mp_. Property name: " + keyToCheck);
    }
}

template <typename T>
JsonArray^ AppendNumberToJsonArray(IVector<T>^ values)
{
    JsonArray^ target = ref new JsonArray();
    for (const auto& value : values)
    {
        target->Append(JsonValue::CreateNumberValue((double)value));
    }

    return target;
}

MixpanelClient::MixpanelClient(String^ token) :
    m_userAgent(ref new HttpProductInfoHeaderValue(L"Codevoid.Utilities.MixpanelClient", L"1.0")),
    m_trackUploadWorker(
        [this](const auto& items, const auto& shouldContinueProcessing) -> auto {
            // Not using std::bind, because ref classes & it don't play nice
            return this->HandleBatchUploadWithUri(m_trackEventUri, items, shouldContinueProcessing);
        },
        [this](const auto& items) -> void {
            MixpanelClient::HandleCompletedUploadsForQueue(*m_trackStorageQueue, items);
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

void MixpanelClient::StartWorkers()
{
    m_trackUploadWorker.Start();
    m_trackStorageQueue->EnableQueuingToStorage();
}

void MixpanelClient::Start()
{
    this->StartWorkers();
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

    this->PauseWorkers().then([deferral]() {
        deferral->Complete();
    });
}

void MixpanelClient::HandleApplicationResuming(Object^, Object^)
{
    this->StartWorkers();
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

task<void> MixpanelClient::PauseWorkers()
{
    m_trackUploadWorker.Pause();
    return m_trackStorageQueue->PersistAllQueuedItemsToStorageAndShutdown();
}

IAsyncAction^ MixpanelClient::PauseAsync()
{
    return create_async([this]() {
        this->PauseWorkers().wait();
    });
}

task<void> MixpanelClient::Shutdown()
{
    // The upload queue can be stuck in long running
    // operations, so we dont want it to drain or reach
    // a 'safe' place. We want it to give up as soon as
    // we're trying to get out of dodge.
    m_trackUploadWorker.ShutdownAndDrop();

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

    if (m_trackStorageQueue == nullptr)
    {
        return;
    }

    this->EndSessionTracking();

    co_await m_trackStorageQueue->PersistAllQueuedItemsToStorageAndShutdown();
    m_trackStorageQueue = nullptr;
}

IAsyncAction^ MixpanelClient::ClearStorageAsync()
{
    this->ThrowIfNotInitialized();
    return create_async([this]() {
        return m_trackStorageQueue->Clear();
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
        this->AddItemsToTrackQueue(previousItems);
    }
}

void MixpanelClient::Initialize(StorageFolder^ queueFolder,
                                Uri^ serviceUri)
{
    m_trackStorageQueue = make_unique<EventStorageQueue>(queueFolder, [this](auto writtenItems) {
        if (m_writtenToStorageMockCallback == nullptr)
        {
            this->AddItemsToTrackQueue(writtenItems);
            return;
        }

        m_writtenToStorageMockCallback(writtenItems);
    });

    m_trackEventUri = serviceUri;
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

    properties = this->EmbelishPropertySetForTrack(properties);
    this->AddDurationForTrack(name, properties);

    IJsonValue^ payload = MixpanelClient::GenerateTrackJsonPayload(name, properties);
    m_trackStorageQueue->QueueEventToStorage(payload);
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

void MixpanelClient::AddItemsToTrackQueue(const vector<shared_ptr<PayloadContainer>>& itemsToUpload)
{
    // If there are any normal priority items, we'll make the work we queue
    // as normal too -- but otherwise, no point in waking up the network stack
    // to process the low priority items.
    bool anyNormalPriorityItems = any_of(begin(itemsToUpload), end(itemsToUpload), [](auto item) -> bool {
        return item->Priority == EventPriority::Normal;
    });

    m_trackUploadWorker.AddWork(itemsToUpload, anyNormalPriorityItems ? WorkPriority::Normal : WorkPriority::Low);
}

void MixpanelClient::HandleCompletedUploadsForQueue(EventStorageQueue& queue, const vector<shared_ptr<PayloadContainer>>& items)
{
    for (auto&& item : items)
    {
        queue.RemoveEventFromStorage(*item).get();
    }
}

vector<shared_ptr<PayloadContainer>> MixpanelClient::HandleBatchUploadWithUri(Uri^ destination, const vector<shared_ptr<PayloadContainer>>& items, const function<bool()>& /*shouldKeepProcessing*/)
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
        if (!this->PostPayloadToUri(destination, eventPayload).get())
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

task<bool> MixpanelClient::PostPayloadToUri(Uri^ destination, const vector<IJsonValue^>& dataItems)
{
    auto jsonEvents = ref new JsonArray();
    
    for (auto&& payload : dataItems)
    {
        jsonEvents->Append(payload);
    }

    auto formPayload = ref new Map<String^, IJsonValue^>();
    formPayload->Insert(L"data", jsonEvents);

    return co_await m_requestHelper(destination, formPayload, m_userAgent);
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

        // IsSuccessStatusCode defines success as 200-299 inclusive
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
    if (m_superProperties != nullptr)
    {
        return m_superProperties;
    }

    if (!this->PersistSuperPropertiesToApplicationData)
    {
        m_superProperties = ref new ValueSet();
        return m_superProperties;
    }

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

    return m_superProperties;
}

void MixpanelClient::ClearSuperProperties()
{
    this->InitializeSuperPropertyCollection();

    String^ distinctId = this->GetDistinctId();

    m_superProperties->Clear();
    m_superProperties = nullptr;

    if (!distinctId->IsEmpty())
    {
        this->SetUserIdentityExplicitly(distinctId);
    }
}

IPropertySet^ MixpanelClient::EmbelishPropertySetForTrack(IPropertySet^ properties)
{
    this->InitializeSuperPropertyCollection();

    // Copy them from the super properties, so that any that are explicitly
    // set by the caller, override those super properties ('cause they're
    // applied later)
    auto embelishedProperties = CopyOrCreatePropertySet(m_superProperties);
    if (this->AutomaticallyAttachTimeToEvents)
    {
        auto now = time_point_cast<milliseconds>(system_clock::now()).time_since_epoch().count();
        embelishedProperties->Insert(L"time", static_cast<double>(now));
    }

    // Merge the properties provided into our clone
    MergePropertySet(embelishedProperties, properties);

    // The properties payload is expected to have the API Token, rather than
    // in the general payload properties. So, lets explicitly add it after
    // everything else so it doesn't get squashed
    embelishedProperties->Insert(L"token", m_token);

    return embelishedProperties;
}

IPropertySet^ MixpanelClient::GetEngageProperties(IPropertySet^ options)
{
    IPropertySet^ properties = CopyOrCreatePropertySet(options);

    if (!this->HasUserIdentity())
    {
        throw ref new InvalidArgumentException(L"Must set a user identity before configuring user profile");
    }

    properties->Insert(StringReference(DISTINCT_ID_PROPERTY_NAME_ENGAGE), this->GetDistinctId());
    properties->Insert(StringReference(TOKEN_PROPERTY_NAME_ENGAGE), m_token);

    return properties;
}

void MixpanelClient::AddDurationForTrack(String^ name, IPropertySet^ properties)
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

JsonObject^ MixpanelClient::GenerateTrackJsonPayload(String^ name, IPropertySet^ properties)
{
    JsonObject^ propertiesPayload = ref new JsonObject();
    MixpanelClient::AppendPropertySetToJsonPayload(properties, propertiesPayload);

    JsonObject^ trackPayload = ref new JsonObject();
    trackPayload->Insert(L"event", JsonValue::CreateStringValue(name));
    trackPayload->Insert(L"properties", propertiesPayload);

    return trackPayload;
}

JsonObject^ MixpanelClient::GenerateEngageJsonPayload(EngageOperationType operation, IPropertySet^ values, IPropertySet^ options)
{
    JsonObject^ engagePayload = ref new JsonObject();
    MixpanelClient::AppendPropertySetToJsonPayload(options, engagePayload);

    IJsonValue^ operationValues = ref new JsonObject();
    MixpanelClient::AppendPropertySetToJsonPayload(values, static_cast<JsonObject^>(operationValues));
    
    String^ operationName = nullptr;

    switch (operation)
    {
        case EngageOperationType::Set:
            operationName = L"$set";
            break;

        case EngageOperationType::Set_Once:
            operationName = L"$set_once";
            break;

        case EngageOperationType::Append:
            operationName = L"$append";
            break;

        case EngageOperationType::Add:
            // Addition operation in MixPanel only supports numerics, so
            // this will restrict that set at calling time, rather than
            // having the service reject it for badness later
            operationValues = ref new JsonObject();
            MixpanelClient::AppendNumericPropertySetToJsonPayload(values, static_cast<JsonObject^>(operationValues));
            operationName = L"$add";
            break;

        case EngageOperationType::Union:
            operationName = L"$union";
            break;

        case EngageOperationType::Remove:
            operationName = L"$remove";
            break;

        case EngageOperationType::Unset:
            operationName = L"$unset";
            JsonArray^ fieldsToUnset = ref new JsonArray();
            for (const auto& value : values) {
                fieldsToUnset->Append(JsonValue::CreateStringValue(value->Key));
            }

            operationValues = fieldsToUnset;
            break;
    }

    engagePayload->Insert(operationName, operationValues);

    return engagePayload;
}

void MixpanelClient::AppendPropertySetToJsonPayload(IPropertySet^ properties, JsonObject^ toAppendTo)
{
    for (const auto& kvp : properties)
    {
        String^ key = kvp->Key;
        ThrowIfPrefixedWithMp(key);

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

        IVector<String^>^ candidateStringVector = dynamic_cast<IVector<String^>^>(kvp->Value);
        if (candidateStringVector != nullptr)
        {
            JsonArray^ stringArray = ref new JsonArray();
            for (const auto& value : candidateStringVector)
            {
                stringArray->Append(JsonValue::CreateStringValue(value));
            }

            toAppendTo->Insert(kvp->Key, stringArray);
            continue;
        }

        IVector<int>^ candidateIntegerVector = dynamic_cast<IVector<int>^>(kvp->Value);
        if (candidateIntegerVector != nullptr)
        {
            toAppendTo->Insert(kvp->Key, AppendNumberToJsonArray(candidateIntegerVector));
            continue;
        }

        IVector<double>^ candidateDoubleVector = dynamic_cast<IVector<double>^>(kvp->Value);
        if (candidateDoubleVector != nullptr)
        {
            toAppendTo->Insert(kvp->Key, AppendNumberToJsonArray(candidateDoubleVector));
            continue;
        }

        IVector<float>^ candidateFloatVector = dynamic_cast<IVector<float>^>(kvp->Value);
        if (candidateFloatVector != nullptr)
        {
            toAppendTo->Insert(kvp->Key, AppendNumberToJsonArray(candidateFloatVector));
            continue;
        }

        throw ref new InvalidCastException(L"Property set includes unsupported data type: " + kvp->Key);
    }
}

void MixpanelClient::AppendNumericPropertySetToJsonPayload(IPropertySet^ properties, JsonObject^ toAppendTo)
{
    for (const auto& kvp : properties)
    {
        ThrowIfPrefixedWithMp(kvp->Key);

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

        throw ref new InvalidCastException(L"Property set includes non-numeric data type: " + kvp->Key);
    }
}

void MixpanelClient::SetUserIdentityExplicitly(String^ identity)
{
    this->InitializeSuperPropertyCollection();
    this->SetSuperPropertyAsString(StringReference(DISTINCT_ID_PROPERTY_NAME), identity);
}

void MixpanelClient::GenerateAndSetUserIdentity()
{
    auto autoGeneratedId = GenerateGuidAsString();
    this->SetUserIdentityExplicitly(autoGeneratedId);
}

bool MixpanelClient::HasUserIdentity()
{
    return !this->GetDistinctId()->IsEmpty();
}

void MixpanelClient::ClearUserIdentity()
{
    this->InitializeSuperPropertyCollection();
    this->RemoveSuperProperty(StringReference(DISTINCT_ID_PROPERTY_NAME));
}

String^ MixpanelClient::GetDistinctId()
{
    this->InitializeSuperPropertyCollection();

    static auto distinctProperty = StringReference(DISTINCT_ID_PROPERTY_NAME);
    if (!this->HasSuperProperty(distinctProperty))
    {
        return nullptr;
    }

    String^ distinctId = this->GetSuperPropertyAsString(distinctProperty);
    return distinctId;
}

void MixpanelClient::ThrowIfNotInitialized()
{
    if (m_trackStorageQueue != nullptr)
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
    m_trackStorageQueue->DontWriteToStorageForTestPurposeses();
    m_trackStorageQueue->SetWriteToStorageIdleLimits(idleTimeout, itemThreshold);
    m_trackUploadWorker.SetIdleTimeout(idleTimeout);
    m_trackUploadWorker.SetItemThreshold(itemThreshold);
}

void MixpanelClient::ForceWritingToStorage()
{
    m_trackStorageQueue->NoReallyWriteToStorageDuringTesting();
}
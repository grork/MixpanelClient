#include "pch.h"
#include "EventStorageQueue.h"
#include "MixpanelClient.h"
#include "PayloadEncoder.h"
#include <chrono>

using namespace CodevoidN::Utilities::Mixpanel;
using namespace concurrency;
using namespace Platform;
using namespace Platform::Collections;
using namespace std;
using namespace std::chrono;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Storage;
using namespace Windows::Web::Http;
using namespace Windows::Web::Http::Headers;

constexpr wchar_t MIXPANEL_TRACK_BASE_URL[] = L"https://api.mixpanel.com/track";

#define WINDOWS_TICK 10000000
#define SEC_TO_UNIX_EPOCH 11644473600LL
#define DEFAULT_TOKEN L"DEFAULT_TOKEN"
#define SUPER_PROPERTIES_CONTAINER_NAME L"Codevoid_Utilities_Mixpanel"

constexpr vector<shared_ptr<PayloadContainer>>::difference_type DEFAULT_UPLOAD_SIZE_STRIDE = 50;

// Sourced from:
// http://stackoverflow.com/questions/6161776/convert-windows-filetime-to-second-in-unix-linux
unsigned CodevoidN::Utilities::Mixpanel::WindowsTickToUnixSeconds(const long long windowsTicks)
{
    return (unsigned)(windowsTicks / WINDOWS_TICK - SEC_TO_UNIX_EPOCH);
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
}

IAsyncAction^ MixpanelClient::InitializeAsync()
{
    return create_async([this]() {
        this->Initialize().wait();
    });
}

void MixpanelClient::Start()
{
    m_uploadWorker.Start();
    m_eventStorageQueue->EnableQueuingToStorage();
}

IAsyncAction^ MixpanelClient::Pause()
{
    return create_async([this]() {
        m_uploadWorker.Pause();
        return m_eventStorageQueue->PersistAllQueuedItemsToStorageAndShutdown();
    });
}

task<void> MixpanelClient::Shutdown()
{
    // The upload queue can be stuck in long running
    // operations, so we dont want it to drain or reach
    // a 'safe' place. We want it to give up as soon as
    // we're trying to get out of dodge.
    m_uploadWorker.ShutdownAndDrop();

    if (m_eventStorageQueue == nullptr)
    {
        return;
    }

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
    auto folder = co_await ApplicationData::Current->LocalFolder->CreateFolderAsync("MixpanelUploadQueue",
        CreationCollisionOption::OpenIfExists);

    this->Initialize(folder, ref new Uri(StringReference(MIXPANEL_TRACK_BASE_URL)));
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

    if (name->IsEmpty())
    {
        throw ref new InvalidArgumentException(L"Name cannot be empty or null");
    }

    if (properties == nullptr)
    {
        properties = ref new PropertySet();
    }

    IJsonValue^ payload = this->GenerateTrackingJsonPayload(name, properties);
    m_eventStorageQueue->QueueEventToStorage(payload);
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

void MixpanelClient::SetSuperProperty(String^ name, String^ value)
{
    this->InitializeSuperPropertyCollection();
    m_superProperties->Insert(name, value);
}

void MixpanelClient::SetSuperProperty(String^ name, double value)
{
    this->InitializeSuperPropertyCollection();
    m_superProperties->Insert(name, value);
}

void MixpanelClient::SetSuperProperty(String^ name, bool value)
{
    this->InitializeSuperPropertyCollection();
    m_superProperties->Insert(name, value);
}

String^ MixpanelClient::GetSuperPropertyAsString(String^ name)
{
    this->InitializeSuperPropertyCollection();
    return static_cast<String^>(m_superProperties->Lookup(name));
}

double MixpanelClient::GetSuperPropertyAsDouble(String^ name)
{
    this->InitializeSuperPropertyCollection();
    return static_cast<double>(m_superProperties->Lookup(name));
}

bool MixpanelClient::GetSuperPropertyAsBool(String^ name)
{
    this->InitializeSuperPropertyCollection();
    return static_cast<bool>(m_superProperties->Lookup(name));
}

bool MixpanelClient::HasSuperProperty(String^ name)
{
    this->InitializeSuperPropertyCollection();
    if (!m_superProperties)
    {
        return false;
    }

    return m_superProperties->HasKey(name);
}

void MixpanelClient::InitializeSuperPropertyCollection()
{
    if (m_superProperties != nullptr)
    {
        return;
    }

    if (this->PersistSuperPropertiesToApplicationData)
    {
        auto localSettings = ApplicationData::Current->LocalSettings;
        auto superProperties = localSettings->CreateContainer(SUPER_PROPERTIES_CONTAINER_NAME, ApplicationDataCreateDisposition::Always);
        m_superProperties = superProperties->Values;
    }
    else
    {
        m_superProperties = ref new ValueSet();
    }
}

void MixpanelClient::ClearSuperProperties()
{
    m_superProperties->Clear();
    m_superProperties = nullptr;
}

JsonObject^ MixpanelClient::GenerateTrackingJsonPayload(String^ name, IPropertySet^ properties)
{
    JsonObject^ propertiesPayload = ref new JsonObject();
    MixpanelClient::AppendPropertySetToJsonPayload(properties, propertiesPayload);

    // The properties payload is expected to have the API Token, rather than
    // in the general payload properties. So, lets explicitly add it.
    propertiesPayload->Insert(L"token", JsonValue::CreateStringValue(m_token));

    if (this->AutomaticallyAttachTimeToEvents && (properties && !properties->HasKey(L"time")))
    {
        auto now = time_point_cast<milliseconds>(system_clock::now()).time_since_epoch().count();
        propertiesPayload->SetNamedValue(L"time", JsonValue::CreateNumberValue(static_cast<double>(now)));
    }

    if (m_superProperties)
    {
        MixpanelClient::AppendPropertySetToJsonPayload(m_superProperties, propertiesPayload);
    }

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
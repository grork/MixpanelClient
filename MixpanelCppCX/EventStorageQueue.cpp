#include "pch.h"
#include "BackgroundWorker.h"
#include "EventStorageQueue.h"
#include "Tracing.h"

using namespace Codevoid::Utilities::Mixpanel;
using namespace concurrency;
using namespace Platform;
using namespace std;
using namespace std::chrono;
using namespace Windows::Storage;
using namespace Windows::Data::Json;

using PayloadContainer_ptr = shared_ptr<PayloadContainer>;
using PayloadContainers = vector<PayloadContainer_ptr>;

String^ Codevoid::Utilities::Mixpanel::GetFileNameForId(const long long& id)
{
    return ref new String(to_wstring(id).append(L".json").c_str());
}

EventStorageQueue::EventStorageQueue(
    StorageFolder^ localStorage,
    function<void(const vector<shared_ptr<PayloadContainer>>&)> writtenToStorageCallback
) :
    m_localStorage(localStorage),
    m_state(QueueState::None),
    m_writtenToStorageCallback(writtenToStorageCallback),
    m_dontWriteToStorageForTestPurposes(false),
    m_writeToStorageWorker(
        bind(&EventStorageQueue::WriteItemsToStorage, this, placeholders::_1, placeholders::_2),
        bind(&EventStorageQueue::HandleProcessedItems, this, placeholders::_1),
        wstring(L"WriteToStorage")
    )
{
    if (localStorage == nullptr)
    {
        throw invalid_argument("Must provide local storage folder");
    }

    TRACE_OUT(L"Event Queue Constructed");

    // Initialize our base ID for saving events to disk to ensure we avoid clashes with
    // multiple concurrent callers generating items at the same moment.
    m_baseId = time_point_cast<milliseconds>(system_clock::now()).time_since_epoch().count();
}

EventStorageQueue::~EventStorageQueue()
{
    TRACE_OUT(L"Event Queue being destroyed");

    this->PersistAllQueuedItemsToStorageAndShutdown().wait();
}

long long EventStorageQueue::GetNextId()
{
    // This is intended to use atomic to allow for
    // lock-less increment.
    auto newId = (m_baseId += 1);
    return newId;
}

size_t EventStorageQueue::GetWaitingToWriteToStorageLength()
{
    return m_writeToStorageWorker.GetQueueLength();
}

long long EventStorageQueue::QueueEventToStorage(IJsonValue^ payload, const EventPriority& priority)
{
    if (m_state > QueueState::Running)
    {
        TRACE_OUT(L"Event dropped due to shutting down");
        return 0;
    }

    auto id = this->GetNextId();
    auto item = make_shared<PayloadContainer>(id, payload, priority);

    TRACE_OUT(L"Event Queued: " + id);
    m_writeToStorageWorker.AddWork(item, (item->Priority == EventPriority::Low ? WorkPriority::Low : WorkPriority::Normal));

    return id;
}

task<vector<shared_ptr<PayloadContainer>>> EventStorageQueue::LoadItemsFromStorage(StorageFolder^ sourceFolder)
{
    TRACE_OUT(L"Restoring items from storage");
    auto files = co_await sourceFolder->GetFilesAsync();
    PayloadContainers loadedPayload;

    for (auto&& file : files)
    {
        TRACE_OUT(L"Reading from storage:" + file->Path);
        auto contents = co_await FileIO::ReadTextAsync(file);

        JsonObject^ payload = nullptr;
        bool successfullyParsed = false;

        // There are situations where the file gets corrupted
        // on disk. This will cause bad things to happen.
        if (!contents->IsEmpty())
        {
            // If the file is there, has contents but *isn't*
            // correct JSON, it'll fail to parse. If it does
            // we'll just move on past this file
            successfullyParsed = JsonObject::TryParse(contents, &payload);
        }

        if (!successfullyParsed || (payload == nullptr))
        {
            create_task(file->DeleteAsync()).wait();
            continue;
        }

        // Convert the file name to the ID
        // This assumes the data is constant, and that wcstoll will stop
        // when it finds a non-numeric char and give me a number that we need
        auto rawString = file->Name->Data();
        auto id = std::wcstoll(rawString, nullptr, 0);

        // Note, it's assumed that items being restored from disk have lasted longer
        // than a few seconds (E.g. across an app restart), we probably want to get
        // it to the network now.
        loadedPayload.emplace_back(make_shared<PayloadContainer>(id, payload, EventPriority::Normal));
    }

    // Load the items loaded from storage into the upload queue.
    // Theres no need to put them in the waiting for storage queue (where new items
    // normally show up), because they're already on storage.
    TRACE_OUT(L"Calling Processed Items Handler");
    
    return loadedPayload;
}

void EventStorageQueue::EnableQueuingToStorage()
{
    m_state = QueueState::Running;
    m_writeToStorageWorker.Start();
}

PayloadContainers EventStorageQueue::WriteItemsToStorage(const PayloadContainers& items, const function<bool()>& shouldKeepProcessing)
{
    PayloadContainers processedItems;

    for (auto&& item : items)
    {
        if (!shouldKeepProcessing())
        {
            break;
        }

        bool didWrite = true;
        if (!m_dontWriteToStorageForTestPurposes)
        {
            didWrite = this->WriteItemToStorage(item).get();
        }

        processedItems.emplace_back(item);

        if (!didWrite) {
            TRACE_OUT(L"Item couldn't be persisted to disk");
        }
    }

    return processedItems;
}

void EventStorageQueue::HandleProcessedItems(const PayloadContainers& itemsWrittenToStorage)
{
    TRACE_OUT(L"Calling Written To Storage Callback");
    if (m_writtenToStorageCallback != nullptr)
    {
        this->m_writtenToStorageCallback(itemsWrittenToStorage);
    }
}

task<void> EventStorageQueue::PersistAllQueuedItemsToStorageAndShutdown()
{
    m_state = QueueState::Drain;

    return create_task([this]() {
        this->m_writeToStorageWorker.Shutdown();
        this->m_state = QueueState::Stopped;
    });
}

task<void> EventStorageQueue::Clear()
{
    m_writeToStorageWorker.Clear();

    co_await this->ClearStorage();
}

task<bool> EventStorageQueue::WriteItemToStorage(const PayloadContainer_ptr item)
{
    TRACE_OUT(L"Writing File: " + GetFileNameForId(item->Id));
    IJsonValue^ payload = item->Payload;

    try {
        auto file = co_await m_localStorage->CreateFileAsync(GetFileNameForId(item->Id));
        co_await FileIO::WriteTextAsync(file, payload->Stringify());
    }
    catch (Exception^ e) {
        TRACE_OUT(L"Failed to write file: " + e->Message);
        return false;
    }

    return true;
}

task<void> EventStorageQueue::ClearStorage()
{
    auto files = co_await m_localStorage->GetFilesAsync();
    for (auto&& file : files)
    {
        try
        {
            co_await file->DeleteAsync();
        }
        catch (COMException^ e)
        {
            if (e->HResult != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
            {
                throw e;
            }
        }
    }
}

task<void> EventStorageQueue::RemoveEventFromStorage(PayloadContainer& itemToRemove)
{
    TRACE_OUT(L"Removing File: " + GetFileNameForId(itemToRemove.Id));

    auto fileToDelete = co_await m_localStorage->TryGetItemAsync(GetFileNameForId(itemToRemove.Id));
    if (fileToDelete == nullptr)
    {
        return;
    }

    try
    {
        co_await fileToDelete->DeleteAsync();
    }
    catch (COMException^ e)
    {
        if (e->HResult != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
        {
            throw e;
        }
    }
}

void EventStorageQueue::SetWriteToStorageIdleLimits(const std::chrono::milliseconds& idleTimeout, const size_t& idleItemThreshold)
{
    m_writeToStorageWorker.SetIdleTimeout(idleTimeout);
    m_writeToStorageWorker.SetItemThreshold(idleItemThreshold);
}

void EventStorageQueue::DontWriteToStorageFolder()
{
    m_dontWriteToStorageForTestPurposes = true;
}

void EventStorageQueue::NoReallyWriteToStorageDuringTesting()
{
    m_dontWriteToStorageForTestPurposes = false;
}
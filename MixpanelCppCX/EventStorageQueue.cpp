#include "pch.h"
#include "BackgroundWorker.h"
#include "EventStorageQueue.h"
#include "Tracing.h"

using namespace CodevoidN::Utilities::Mixpanel;
using namespace concurrency;
using namespace Platform;
using namespace std;
using namespace std::chrono;
using namespace Windows::Storage;
using namespace Windows::Data::Json;

using PayloadContainer_ptr = shared_ptr<PayloadContainer>;
using PayloadContainers = vector<PayloadContainer_ptr>;

String^ GetFileNameForId(const long long& id)
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

    TRACE_OUT("Event Queue Constructed");

    // Initialize our base ID for saving events to disk to ensure we avoid clashes with
    // multiple concurrent callers generating items at the same moment.
    m_baseId = time_point_cast<milliseconds>(system_clock::now()).time_since_epoch().count();
}

EventStorageQueue::~EventStorageQueue()
{
    TRACE_OUT("Event Queue being destroyed");

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
        TRACE_OUT("Event dropped due to shutting down");
        return 0;
    }

    auto id = this->GetNextId();
    auto item = make_shared<PayloadContainer>(id, payload, priority);

    TRACE_OUT("Event Queued: " + id);
    m_writeToStorageWorker.AddWork(item, (item->Priority == EventPriority::Low ? WorkPriority::Low : WorkPriority::Normal));

    return id;
}

task<vector<shared_ptr<PayloadContainer>>> EventStorageQueue::LoadItemsFromStorage(StorageFolder^ sourceFolder)
{
    TRACE_OUT("Restoring items from storage");
    auto files = co_await sourceFolder->GetFilesAsync();
    PayloadContainers loadedPayload;

    for (auto&& file : files)
    {
        TRACE_OUT("Reading from storage:" + file->Path);
        auto contents = co_await FileIO::ReadTextAsync(file);
        auto payload = JsonObject::Parse(contents);

        // Convert the file name to the ID
        // This assumes the data is constant, and that wcstoll will stop
        // when it finds a non-numeric char and give me a number that we need
        auto rawString = file->Name->Data();
        auto id = std::wcstoll(rawString, nullptr, 0);
        // Note, it's assumed that items being restored from disk have lasted longer
        // than a few seconds (E.g. across an app restart), we probably want  to get
        // it to the network now.
        loadedPayload.emplace_back(make_shared<PayloadContainer>(id, payload, EventPriority::Normal));
    }

    // Load the items loaded from storage into the upload queue.
    // Theres no need to put them in the waiting for storage queue (where new items
    // normally show up), because they're already on storage.
    TRACE_OUT("Calling Processed Items Handler");
    
    return loadedPayload;
}

void EventStorageQueue::EnableQueuingToStorage()
{
    m_writeToStorageWorker.Start();
}

PayloadContainers EventStorageQueue::WriteItemsToStorage(const PayloadContainers& items, const function<bool()>& shouldKeepProcessing)
{
    PayloadContainers successfullyProcessedItems;

    for (auto&& item : items)
    {
        if (!shouldKeepProcessing())
        {
            break;
        }

        this->WriteItemToStorage(item).wait();
        successfullyProcessedItems.emplace_back(item);
    }

    return successfullyProcessedItems;
}

void EventStorageQueue::HandleProcessedItems(const PayloadContainers& itemsWrittenToStorage)
{
    TRACE_OUT("Calling Written To Storage Callback");
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

task<void> EventStorageQueue::WriteItemToStorage(const PayloadContainer_ptr item)
{
    TRACE_OUT("Writing File: " + GetFileNameForId(item->Id));
    IJsonValue^ payload = item->Payload;

    auto file = co_await m_localStorage->CreateFileAsync(GetFileNameForId(item->Id));
    co_await FileIO::WriteTextAsync(file, payload->Stringify());
}

task<void> EventStorageQueue::ClearStorage()
{
    auto files = co_await m_localStorage->GetFilesAsync();
    for (auto&& file : files)
    {
        co_await file->DeleteAsync();
    }
}

void EventStorageQueue::SetWriteToStorageIdleLimits(const std::chrono::milliseconds& idleTimeout, const size_t& idleItemThreshold)
{
    m_writeToStorageWorker.SetIdleTimeout(idleTimeout);
    m_writeToStorageWorker.SetItemThreshold(idleItemThreshold);
}
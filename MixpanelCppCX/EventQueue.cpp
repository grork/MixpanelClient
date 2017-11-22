#include "pch.h"
#include "EventQueue.h"
#include "Tracing.h"

using namespace CodevoidN::Utilities::Mixpanel;
using namespace concurrency;
using namespace Platform;
using namespace std;
using namespace std::chrono;
using namespace Windows::Data::Json;
using namespace Windows::Storage;

using PayloadContainer_ptr = shared_ptr<EventQueue::PayloadContainer>;
using PayloadContainers = vector<PayloadContainer_ptr>;

String^ GetFileNameForId(const long long& id)
{
    return ref new String(to_wstring(id).append(L".json").c_str());
}

EventQueue::PayloadContainer::PayloadContainer(long long id, JsonObject^ payload) :
    Id(id), Payload(payload)
{
}

EventQueue::EventQueue(StorageFolder^ localStorage) :
    m_localStorage(localStorage),
    m_shutdownState(ShutdownState::None),
    m_writeToStorageWorkerStarted(false)
{
    if (localStorage == nullptr)
    {
        throw std::invalid_argument("Must provide local storage folder");
    }

    TRACE_OUT("Event Queue Constructed");

    // Initialize our base ID for saving events to disk to ensure we avoid clashes with
    // multiple concurrent callers generating items at the same moment.
    m_baseId = time_point_cast<milliseconds>(system_clock::now()).time_since_epoch().count();
}

EventQueue::~EventQueue()
{
    TRACE_OUT("Event Queue being destroyed");

    m_shutdownState = ShutdownState::Shutdown;

    if (m_writeToStorageWorker.joinable())
    {
        TRACE_OUT("Joining Write To Storage Worker thread");
        m_writeToStorageQueueReady.notify_one();
        m_writeToStorageWorker.join();
        assert(!m_writeToStorageWorkerStarted);
    }

    TRACE_OUT("Event Queue Destroyed");
}

long long EventQueue::GetNextId()
{
    // This is intended to use atomic to allow for
    // lock-less increment.
    auto newId = (m_baseId += 1);
    return newId;
}

size_t EventQueue::GetWaitingToWriteToStorageLength()
{
    lock_guard<mutex> lock(m_writeToStorageQueueLock);
    return m_writeToStorageQueue.size();
}

size_t EventQueue::GetWaitingForUploadLength()
{
    lock_guard<mutex> lock(m_waitingForUploadQueueLock);
    return m_waitingForUpload.size();
}

bool EventQueue::FindPayloadWithId(const PayloadContainer_ptr& other, const long long id)
{
    return other->Id == id;
}

long long EventQueue::QueueEventForUpload(JsonObject^ payload)
{
    if (m_shutdownState > ShutdownState::None)
    {
        TRACE_OUT("Event dropped due to shutting down");
        return 0;
    }

    auto id = this->GetNextId();
    auto item = make_shared<PayloadContainer>(id, payload);

    TRACE_OUT("Event Queued: " + id);

    {
        lock_guard<mutex> lock(m_writeToStorageQueueLock);
        m_writeToStorageQueue.emplace_back(item);
    }

    m_writeToStorageQueueReady.notify_one();

    return id;
}

task<void> EventQueue::RestorePendingUploadQueueFromStorage()
{
    TRACE_OUT("Restoring items from storage");
    auto files = co_await m_localStorage->GetFilesAsync();
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
        loadedPayload.emplace_back(make_shared<PayloadContainer>(id, payload));
    }

    // Load the items loaded from storage into the upload queue.
    // Theres no need to put them in the waiting for storage queue (where new items
    // normally show up), because they're already on storage.
    {
        TRACE_OUT("Adding Items to upload queue");
        lock_guard<mutex> lock(m_waitingForUploadQueueLock);
        m_waitingForUpload.reserve(m_waitingForUpload.size() + loadedPayload.size());
        m_waitingForUpload.insert(end(m_waitingForUpload), begin(loadedPayload), end(loadedPayload));
    }
}

void EventQueue::EnableQueuingToStorage()
{
    m_queueToStorage = true;
    if (m_writeToStorageWorkerStarted)
    {
        TRACE_OUT("Write To Storage worker already running");
        return;
    }

    TRACE_OUT("Starting Write To Storage worker");
    m_writeToStorageWorkerStarted = true;
    m_writeToStorageWorker = thread(&EventQueue::PersistToStorageWorker, this);
}

void EventQueue::PersistToStorageWorker()
{
    while (m_shutdownState < ShutdownState::Drop)
    {
        TRACE_OUT("Write To Storage Iterating");
        PayloadContainers itemsToPersist;

        {
            unique_lock<mutex> lock(m_writeToStorageQueueLock);

            // If we don't have anything in the queue, lets wait for something
            // to be in it, or to shutdown.
            if (m_writeToStorageQueue.size() < 1)
            {
                TRACE_OUT("Waiting for Items in the Write To Storage Queue");

                m_writeToStorageQueueReady.wait(lock, [this]() {
                    return ((m_writeToStorageQueue.size() > 0) || (m_shutdownState > ShutdownState::None));
                });
            }

            itemsToPersist.assign(begin(m_writeToStorageQueue), end(m_writeToStorageQueue));
        }

        if (itemsToPersist.size() == 0)
        {
            if (m_shutdownState == ShutdownState::Drain)
            {
                m_shutdownState = ShutdownState::Shutdown;
            }

            continue;
        }

        TRACE_OUT("Writing Payloads to Disk");
        for (auto&& item : itemsToPersist)
        {
            if (*m_shutdownState > ShutdownState::Drain)
            {
                break;
            }

            this->WriteItemToStorage(item).wait();
        }

        // Remove the items from the queue
        {
            lock_guard<mutex> lock(m_writeToStorageQueueLock);

            TRACE_OUT("Clearing Write to Storage Queue");

            // Get the position of the first & last elements in our snapped
            // queue copy, so we can purge them from the waiting queue now.
            // This assumes that the vector hasn't been trimmed e.g. it was
            // only added to, then remove the first/last in this list from
            // the original list.
            auto first = find(m_writeToStorageQueue.begin(), m_writeToStorageQueue.end(), itemsToPersist.front());
            auto last = find(m_writeToStorageQueue.begin(), m_writeToStorageQueue.end(), itemsToPersist.back());

            // Make sure they were actually found in the list.
            assert(first != m_writeToStorageQueue.end());
            assert(last != m_writeToStorageQueue.end());

            // Erase this range, being aware of erase not deleting the item
            // pointed to by last, so increment it to actually erase it
            m_writeToStorageQueue.erase(first, last + 1);
        }

        if (m_shutdownState > ShutdownState::None)
        {
            continue;
        }

        TRACE_OUT("Adding persisted items to the upload queue");
        lock_guard<mutex> uploadLock(m_waitingForUploadQueueLock);
        m_waitingForUpload.reserve(m_waitingForUpload.size() + itemsToPersist.size());
        m_waitingForUpload.insert(end(m_waitingForUpload), begin(itemsToPersist), end(itemsToPersist));
    }

    m_writeToStorageWorkerStarted = false;
}

task<void> EventQueue::PersistAllQueuedItemsToStorageAndShutdown()
{
    m_shutdownState = ShutdownState::Drain;

    return create_task([this]() {
        if (!m_writeToStorageWorker.joinable())
        {
            return;
        }

        m_writeToStorageWorker.join();
    });
}

task<void> EventQueue::Clear()
{
    {
        // This is a scoped lock, because the lock is only
        // held for 1 thread -- not across threads. Since the
        // deletion here happens separately from the queue it's
        // ok to hold the lock only while we clear things from the
        // in memory list.
        lock_guard<mutex> lock(m_writeToStorageQueueLock);
        m_writeToStorageQueue.clear();
    }

    co_await this->ClearStorage();
}

task<void> EventQueue::WriteItemToStorage(PayloadContainer_ptr item)
{
    if (!m_queueToStorage)
    {
        return;
    }

    TRACE_OUT("Writing File: " + GetFileNameForId(item->Id));
    JsonObject^ payload = item->Payload;

    auto file = co_await m_localStorage->CreateFileAsync(GetFileNameForId(item->Id));
    co_await FileIO::WriteTextAsync(file, payload->Stringify());
}

task<void> EventQueue::ClearStorage()
{
    if (!m_queueToStorage)
    {
        return;
    }

    auto files = co_await m_localStorage->GetFilesAsync();
    for (auto&& file : files)
    {
        co_await file->DeleteAsync();
    }
}
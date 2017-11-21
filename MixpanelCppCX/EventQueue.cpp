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

EventQueue::EventQueue(StorageFolder^ localStorage) : m_localStorage(localStorage)
{
    if (localStorage == nullptr)
    {
        throw std::invalid_argument("Must provide local storage folder");
    }

    // Initialize our base ID for saving events to disk to ensure we avoid clashes with
    // multiple concurrent callers generating items at the same moment.
    m_baseId = time_point_cast<milliseconds>(system_clock::now()).time_since_epoch().count();
}

void EventQueue::DisableQueuingToStorage()
{
    m_queueToStorage = false;
}

void EventQueue::EnableQueuingToStorage()
{
    m_queueToStorage = true;
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
    return m_waitingToWriteToStorage.size();
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
    auto id = this->GetNextId();
    auto item = make_shared<PayloadContainer>(id, payload);

    TRACE_OUT("Event Queued: " + id);

    {
        lock_guard<mutex> lock(m_writeToStorageQueueLock);
        m_waitingToWriteToStorage.emplace_back(item);
    }

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

task<void> EventQueue::PersistAllQueuedItemsToStorage()
{
    TRACE_OUT("Persisting Queue items to Storage");
    co_await this->WriteCurrentQueueStateToStorage(AddToUploadQueue::No);
}

task<void> EventQueue::WriteCurrentQueueStateToStorage(AddToUploadQueue addToUpload)
{
    shared_ptr<PayloadContainers> itemsToWrite;

    // Lock the queue to capture the items.
    {
        lock_guard<mutex> lock(m_writeToStorageQueueLock);
        itemsToWrite = make_shared<PayloadContainers>(m_waitingToWriteToStorage.begin(), m_waitingToWriteToStorage.end());
    }

    auto writeTask = create_task([this, itemsToWrite]() {
        TRACE_OUT("Writing Queued items to storage");
        for (auto&& item : (*itemsToWrite))
        {
            this->WriteItemToStorage(item).wait();
        }
    });

    co_await writeTask;

    // Remove the items from the queue
    {
        lock_guard<mutex> lock(m_writeToStorageQueueLock);
        
        // Get the position of the first & last elements in our snapped
        // queue copy, so we can purge them from the waiting queue now.
        // This assumes that the vector hasn't been trimmed e.g. it was
        // only added to, then remove the first/last in this list from
        // the original list.
        auto first = find(m_waitingToWriteToStorage.begin(), m_waitingToWriteToStorage.end(), (*itemsToWrite).front());
        auto last = find(m_waitingToWriteToStorage.begin(), m_waitingToWriteToStorage.end(), (*itemsToWrite).back());

        // Make sure they were actually found in the list.
        assert(first != m_waitingToWriteToStorage.end());
        assert(last != m_waitingToWriteToStorage.end());
        m_waitingToWriteToStorage.erase(first, last);
    }

    if (addToUpload == AddToUploadQueue::No)
    {
        TRACE_OUT("Written queue to storage, not adding to upload queue");
        return;
    }

    TRACE_OUT("Adding persisted items to the upload queue");
    lock_guard<mutex> uploadLock(m_waitingForUploadQueueLock);
    m_waitingForUpload.reserve(m_waitingForUpload.size() + (*itemsToWrite).size());
    m_waitingForUpload.insert(end(m_waitingForUpload), begin(*itemsToWrite), end(*itemsToWrite));
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
        m_waitingToWriteToStorage.clear();
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
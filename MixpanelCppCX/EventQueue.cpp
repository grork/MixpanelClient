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
    m_queueToDisk = false;
}

void EventQueue::EnableQueuingToStorage()
{
    m_queueToDisk = true;
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
    lock_guard<mutex> lock(m_queueAccessLock);
    return m_waitingToWriteToStorage.size();
}

bool EventQueue::FindPayloadWithId(const std::shared_ptr<PayloadContainer>& other, const long long id)
{
    return other->Id == id;
}

long long EventQueue::QueueEventForUpload(JsonObject^ payload)
{
    auto id = this->GetNextId();
    auto item = make_shared<PayloadContainer>(id, payload);

    {
        lock_guard<mutex> lock(m_queueAccessLock);
        m_waitingToWriteToStorage.emplace_back(item);
    }

    return id;
}

task<void> EventQueue::RestorePendingUploadQueue()
{
    auto files = co_await m_localStorage->GetFilesAsync();
    vector<shared_ptr<PayloadContainer>> loadedPayload;

    for (auto&& file : files)
    {
        auto contents = co_await FileIO::ReadTextAsync(file);
        auto payload = JsonObject::Parse(contents);

        // Convert the file name to the ID
        // This assumes the data is constant, and that wcstoll will stop
        // when it finds a non-numeric char and give me a number that we need
        auto rawString = file->Name->Data();
        auto id = std::wcstoll(rawString, nullptr, 0);
        loadedPayload.emplace_back(make_shared<PayloadContainer>(id, payload));
    }

    {
        lock_guard<mutex> lock(m_queueAccessLock);
        for (auto&& payloadItem : loadedPayload)
        {
            m_waitingToWriteToStorage.emplace_back(payloadItem);
        }
    }
}

task<void> EventQueue::PersistAllQueuedItemsToStorage()
{
    create_task([this]() {
        {
            lock_guard<mutex> lock(m_queueAccessLock);
            for (auto&& file : this->m_waitingToWriteToStorage)
            {
                this->WriteItemToStorage(file).wait();
            }
        }

        this->m_queueDrained.set();
    });

    co_await create_task(m_queueDrained);
}

task<void> EventQueue::Clear()
{
    lock_guard<mutex> lock(m_queueAccessLock);

    m_waitingToWriteToStorage.clear();

    co_await this->ClearStorage();
}

task<void> EventQueue::WriteItemToStorage(shared_ptr<PayloadContainer> item)
{
    if (!m_queueToDisk)
    {
        return;
    }

    TRACE_OUT("Writing File: " + GetFileNameForId(item->Id));
    JsonObject^ payload = item->Payload;

    auto file = co_await m_localStorage->CreateFileAsync(GetFileNameForId(item->Id));
    co_await FileIO::WriteTextAsync(file, payload->Stringify());
}

//task<void> EventQueue::RemoveItemFromStorage(long long id)
//{
//    if(!m_queueToDisk)
//    {
//        return;
//    }
//
//    auto name = GetFileNameForId(id);
//
//    auto file = co_await m_localStorage->TryGetItemAsync(name);
//    if (file == nullptr)
//    {
//        return;
//    }
//
//    co_await file->DeleteAsync();
//}

task<void> EventQueue::ClearStorage()
{
    if (!m_queueToDisk)
    {
        return;
    }

    auto files = co_await m_localStorage->GetFilesAsync();
    for (auto&& file : files)
    {
        co_await file->DeleteAsync();
    }
}
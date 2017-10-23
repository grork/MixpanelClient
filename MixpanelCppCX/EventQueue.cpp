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
    auto newId = (m_baseId += 1);
    return newId;
}

size_t EventQueue::GetQueueLength()
{
	lock_guard<mutex> lock(m_queueAccessLock);
	return m_queue.size();
}

bool EventQueue::find_payload(const std::shared_ptr<PayloadContainer>& other, const long long id)
{
    return other->Id == id;
}

long long EventQueue::QueueEventForUpload(JsonObject^ payload)
{
    auto id = this->GetNextId();
    auto item = make_shared<PayloadContainer>(id, payload);
    m_queue.emplace_back(item);

    return id;
}

void EventQueue::RemoveEventFromUploadQueue(long long id)
{
    auto container = find_if(m_queue.begin(), m_queue.end(), std::bind(&EventQueue::find_payload, placeholders::_1, id));
    if (container == m_queue.end())
    {
        return;
    }

    m_queue.erase(container);
}

task<void> EventQueue::RestorePendingUploadQueue()
{
    auto files = co_await m_localStorage->GetFilesAsync();
    for (auto&& file : files)
    {
        auto contents = co_await FileIO::ReadTextAsync(file);
        auto payload = JsonObject::Parse(contents);

        // Convert the file name to the ID
        // This assumes the data is constant, and that wcstoll will stop
        // when it finds a non-numeric char and give me a number that we need
        auto rawString = file->Name->Data();
        auto id = std::wcstoll(rawString, nullptr, 0);
        m_queue.emplace_back(make_shared<PayloadContainer>(id, payload));
    }
}

task<void> EventQueue::PersistAllQueuedItemsToStorage()
{
    for (auto&& item : m_queue)
    {
        co_await this->WriteItemToStorage(item);
    }
}

task<void> EventQueue::Clear()
{
    m_queue.clear();

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
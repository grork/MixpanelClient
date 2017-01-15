#include "pch.h"
#include "EventQueue.h"
#include <chrono>
#include <string>

using namespace CodevoidN::Utilities::Mixpanel;
using namespace concurrency;
using namespace Platform;
using namespace std;
using namespace std::chrono;
using namespace Windows::Data::Json;
using namespace Windows::Storage;

String^ GetFileNameForId(const long long& id)
{
    return ref new String(std::to_wstring(id).append(L".json").c_str());
}

void EventQueue::DisableQueuingToStorage()
{
    m_queueToDisk = false;
}

void EventQueue::EnableQueuingToStorage()
{
    m_queueToDisk = true;
}

long long EventQueue::QueueEvent(JsonObject^ payload)
{
    auto now = time_point_cast<milliseconds>(system_clock::now()).time_since_epoch().count();
    m_queue.emplace_back(PayloadContainer{ now, payload, false });

    PayloadContainer& newlyAddedItem = m_queue.at(m_queue.size() - 1);
    this->QueueToStorage(newlyAddedItem).wait();

    return newlyAddedItem.Id;
}

size_t EventQueue::GetQueueLength()
{
    return m_queue.size();
}

void EventQueue::RemoveEvent(long long id)
{
    auto container = find_if(m_queue.begin(), m_queue.end(), find_payload(id));
    if (container == m_queue.end())
    {
        return;
    }

    m_queue.erase(container);
    this->RemoveFromStorage(id).wait();
}

void EventQueue::Clear()
{
    m_queue.clear();

    this->ClearStorage().wait();
}

task<void> EventQueue::RestoreQueue()
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
        m_queue.emplace_back(PayloadContainer{ id, payload, true });
    }
}

task<void> EventQueue::QueueToStorage(PayloadContainer& item)
{
    if (!m_queueToDisk)
    {
        return;
    }

    JsonObject^ payload = item.Payload;

    auto file = co_await m_localStorage->CreateFileAsync(GetFileNameForId(item.Id));
    co_await FileIO::WriteTextAsync(file, payload->Stringify());

    item.Persisted = true;
}

task<void> EventQueue::RemoveFromStorage(long long id)
{
    if(!m_queueToDisk)
    {
        return;
    }

    auto name = GetFileNameForId(id);

    auto file = co_await m_localStorage->TryGetItemAsync(name);
    if (file == nullptr)
    {
        return;
    }

    co_await file->DeleteAsync();
}

task<void>EventQueue::ClearStorage()
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
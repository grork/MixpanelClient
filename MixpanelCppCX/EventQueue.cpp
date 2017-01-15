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
    this->m_queueToDisk = false;
}

void EventQueue::EnableQueuingToStorage()
{
    this->m_queueToDisk = true;
}

long long EventQueue::QueueEvent(JsonObject^ payload)
{
    auto now = time_point_cast<milliseconds>(system_clock::now()).time_since_epoch().count();
    this->m_queue.emplace_back(PayloadContainer{ now,payload });

    PayloadContainer& newlyAddedItem = m_queue.at(m_queue.size() - 1);
    this->QueueToStorage(newlyAddedItem).wait();

    return newlyAddedItem.Id;
}

size_t EventQueue::GetQueueLength()
{
    return this->m_queue.size();
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

task<void> EventQueue::QueueToStorage(const PayloadContainer& item)
{
    if (!this->m_queueToDisk)
    {
        return;
    }

    JsonObject^ payload = item.Payload;

    auto file = co_await this->m_localStorage->CreateFileAsync(GetFileNameForId(item.Id));
    co_await FileIO::WriteTextAsync(file, payload->Stringify());
}

task<void> EventQueue::RemoveFromStorage(long long id)
{
    if(!this->m_queueToDisk)
    {
        return;
    }

    auto name = GetFileNameForId(id);

    auto file = co_await this->m_localStorage->TryGetItemAsync(name);
    if (file == nullptr)
    {
        return;
    }

    co_await file->DeleteAsync();
}
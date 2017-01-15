#include "pch.h"
#include "EventQueue.h"
#include <chrono>

using namespace CodevoidN::Utilities::Mixpanel;
using namespace Windows::Data::Json;
using namespace std;
using namespace std::chrono;

long long EventQueue::QueueEvent(JsonObject^ payload)
{
    auto now = time_point_cast<milliseconds>(system_clock::now()).time_since_epoch().count();
    this->m_queue.emplace_back(PayloadContainer{ now,payload });

    return now;
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
}
#include "pch.h"
#include "EventQueue.h"

using namespace CodevoidN::Utilities::Mixpanel;
using namespace Windows::Data::Json;

void EventQueue::QueueEvent(JsonObject^ payload)
{
    this->m_queue.emplace_back(payload);
}

size_t EventQueue::GetQueueLength()
{
    return this->m_queue.size();
}
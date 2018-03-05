#include "pch.h"
#include <chrono>
#include <string>
#include "DurationTracker.h"

using namespace Codevoid::Utilities::Mixpanel;
using namespace std;
using namespace std::chrono;

DurationTracker::DurationTracker(const steady_clock::time_point& initialTime) : m_overrideNexTimeAccess(initialTime)
{ }

void DurationTracker::StartTimerFor(const wstring& name)
{
    m_timersForEvents.insert_or_assign(name, this->GetTimePointForNow());
}

std::optional<milliseconds> DurationTracker::EndTimerFor(const wstring& name)
{
    // If the event wasn't tracked, we're going to
    // return an empty optional.
    auto then = m_timersForEvents.find(name);
    if (then == m_timersForEvents.end())
    {
        return nullopt;
    }

    auto now = GetTimePointForNow();
    // Calculate of the event we looked up
    auto durationOfEvent = now - (*then).second;

    // When an event timer is asked for, we also
    // stop tracking it's time. Make sure we delete this
    // after we've used the iterator, otherwise it'll
    // get deallocated
    m_timersForEvents.erase(name);

    return duration_cast<milliseconds>(durationOfEvent);
}

steady_clock::time_point DurationTracker::GetTimePointForNow()
{
    // If we have an override value, we'll return that
    // rather than the real clock.
    if (m_overrideNexTimeAccess.has_value())
    {
        auto value = *m_overrideNexTimeAccess;
        m_overrideNexTimeAccess.reset();
        return value;
    }

    return steady_clock::now();
}

void DurationTracker::__SetClock(const steady_clock::time_point& advanceTo)
{
    m_overrideNexTimeAccess = advanceTo;
}


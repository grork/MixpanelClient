#include "pch.h"
#include <chrono>
#include <string>
#include "DurationTracker.h"

using namespace Codevoid::Utilities::Mixpanel;
using namespace std;
using namespace std::chrono;

optional<steady_clock::time_point> g_overrideNextTimeAccess;

steady_clock::time_point GetTimePointForNow()
{
    // If we have an override value, we'll return that
    // rather than the real clock.
    if (g_overrideNextTimeAccess.has_value())
    {
        auto value = *g_overrideNextTimeAccess;
        g_overrideNextTimeAccess.reset();
        return value;
    }

    return steady_clock::now();
}

void DurationTracker::StartTimerFor(const wstring& name)
{
    m_timersForEvents.try_emplace(name, TrackingTimer { GetTimePointForNow(), milliseconds(0) });
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

    // Calculate total duration of the event we looked up
    auto durationOfEvent = now - (*then).second.start;

    // Remove any adjustment (E.g. while app was suspended)
    // from the duration. Note, if the tracker is currently
    // paused, any time in that paused state is not added
    // onto the event here.
    durationOfEvent -= (*then).second.accumulatedAdjustment;
    
    // When an event timer is asked for, we also
    // stop tracking it's time. Make sure we delete this
    // after we've used the iterator, otherwise it'll
    // get deallocated
    m_timersForEvents.erase(name);

    return duration_cast<milliseconds>(durationOfEvent);
}

void DurationTracker::PauseTimers()
{
    if (m_pausedTime.has_value())
    {
        // We're already paused
        return;
    }

    m_pausedTime = GetTimePointForNow();
}

void DurationTracker::ResumeTimers()
{
    // If we weren't paused, don't do any processing
    if (!m_pausedTime.has_value())
    {
        return;
    }

    // Calculate the duration to apply
    auto now = GetTimePointForNow();
    auto pausedDuration = duration_cast<milliseconds>(now - *m_pausedTime);
    
    // Update all accumulated adjustments in the timers for the time
    // we were paused to ensure their durations are accurate.
    for (auto&& item : m_timersForEvents)
    {
        if (item.second.start > *m_pausedTime)
        {
            // We started to track an event while paused
            // It's implied that if you did this you probably
            // don't want to track the paused time for that event
            continue;
        }

        item.second.accumulatedAdjustment += pausedDuration;
    }

    m_pausedTime.reset();
}

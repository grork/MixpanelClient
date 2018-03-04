#pragma once

#include <chrono>
#include <unordered_map>
#include <optional>
#include <string>

namespace Codevoid::Tests::Mixpanel {
    class DurationTrackerTests;
}

namespace Codevoid::Utilities::Mixpanel {
    class DurationTracker
    {
        friend class Codevoid::Tests::Mixpanel::DurationTrackerTests;

    public:
        /// <summary>
        /// Starts a timer with the given name. The time at the point
        /// this is called for the name will be the starting time for
        /// when EndTimerFor is called when calculating the duration.
        /// </summary>
        void StartTimerFor(const std::wstring& name);

        /// <summary>
        /// Returns the time since a timer was started, accounting for
        /// any "suspended" times during that wall clock time. The timer
        /// is stopped when this is called, so you can't call again to ask
        /// for an updated duration.
        /// </summary>
        std::chrono::milliseconds EndTimerFor(const std::wstring& name);

    private:
        std::unordered_map<std::wstring, std::chrono::steady_clock::time_point> m_timersForEvents;

        /// <summary>
        /// Helper that "hides" the details of the exact clock
        /// that is being used to facilitate testing (so we don't have
        /// to wait for the wall clock time to pass to validate
        /// behaviour
        /// </summary>
        std::chrono::steady_clock::time_point GetTimePointForNow();
       
        // Test helpers to facilitate manipulation of the clock
        // by external parties.
        DurationTracker(const std::chrono::steady_clock::time_point& intialTime);
        void __SetClock(const std::chrono::steady_clock::time_point& nextTime);
        std::optional<std::chrono::steady_clock::time_point> m_overrideNexTimeAccess;
    };
}


#include "pch.h"
#include <optional>
#include "CppUnitTest.h"
#include "DurationTracker.h"

using namespace Codevoid::Utilities::Mixpanel;

using namespace Platform;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace std::chrono;

extern std::optional<steady_clock::time_point> g_overrideNextTimeAccess;

void SetNextClockAccessTime_DurationTracker(const steady_clock::time_point& advanceTo)
{
    g_overrideNextTimeAccess = advanceTo;
}

namespace Codevoid { namespace Tests { namespace Mixpanel {
    TEST_CLASS(DurationTrackerTests)
    {
        DurationTracker tracker;
        steady_clock::time_point now;

    public:
        TEST_METHOD_INITIALIZE(Initialize)
        {
            now = steady_clock::now();
            SetNextClockAccessTime_DurationTracker(now);
        }

        TEST_METHOD(CanTrackTimeForOneEvent)
        {
            tracker.StartTimerFor(L"Test");

            SetNextClockAccessTime_DurationTracker(now + 500ms);
            auto measuredDuration = tracker.EndTimerFor(L"Test");

            Assert::AreEqual(500, (int)(*measuredDuration).count(), L"Duration between start & end was inaccurate");
        }

        TEST_METHOD(EndingTimerForEventThatWasntStartedReturnsEmptyValue)
        {
            auto measuredDuration = tracker.EndTimerFor(L"Test");

            Assert::IsFalse(measuredDuration.has_value(), L"An event that didn't start, shouldn't have a value when ending");
        }

        TEST_METHOD(CanTrackTimeForTwoEvent)
        {
            tracker.StartTimerFor(L"Test");
            
            SetNextClockAccessTime_DurationTracker(now);
            tracker.StartTimerFor(L"Test2");

            SetNextClockAccessTime_DurationTracker(now + 500ms);
            auto measuredDuration = tracker.EndTimerFor(L"Test");
            Assert::AreEqual(500, (int)(*measuredDuration).count(), L"Duration between start & end was inaccurate");

            SetNextClockAccessTime_DurationTracker(now + 750ms);
            measuredDuration = tracker.EndTimerFor(L"Test2");
            Assert::AreEqual(750, (int)(*measuredDuration).count(), L"Duration between start & end on second event was inaccurate");
        }

        TEST_METHOD(TimersAdjustedForPausedTime)
        {
            tracker.StartTimerFor(L"Test");

            SetNextClockAccessTime_DurationTracker(now + 500ms);
            tracker.PauseTimers();

            SetNextClockAccessTime_DurationTracker(now + 10'000ms);
            tracker.ResumeTimers();

            SetNextClockAccessTime_DurationTracker(now + 10'000ms);
            auto measuredDuration = tracker.EndTimerFor(L"Test");

            Assert::AreEqual(500, (int)(*measuredDuration).count(), L"Duration between start & end was inaccurate");
        }

        TEST_METHOD(TimeIsntAdjustedWhenTrackingStartsWhilePaused)
        {
            SetNextClockAccessTime_DurationTracker(now + 500ms);
            tracker.PauseTimers();

            SetNextClockAccessTime_DurationTracker(now + 1'000ms);
            tracker.StartTimerFor(L"Test");

            SetNextClockAccessTime_DurationTracker(now + 10'000ms);
            tracker.ResumeTimers();

            SetNextClockAccessTime_DurationTracker(now + 10'000ms);
            auto measuredDuration = tracker.EndTimerFor(L"Test");

            Assert::AreEqual(9'000, (int)(*measuredDuration).count(), L"Duration between start & end was inaccurate");
        }

        TEST_METHOD(PausingTrackerMoreThanOnceHasNoImpact)
        {
            tracker.StartTimerFor(L"Test");

            SetNextClockAccessTime_DurationTracker(now + 500ms);
            tracker.PauseTimers();

            SetNextClockAccessTime_DurationTracker(now + 9'000ms);
            tracker.PauseTimers();

            SetNextClockAccessTime_DurationTracker(now + 10'000ms);
            tracker.ResumeTimers();

            SetNextClockAccessTime_DurationTracker(now + 10'000ms);
            auto measuredDuration = tracker.EndTimerFor(L"Test");

            Assert::AreEqual(500, (int)(*measuredDuration).count(), L"Duration between start & end was inaccurate");
        }

        TEST_METHOD(ResumingTrackerMoreThanOnceHasNoImpact)
        {
            tracker.StartTimerFor(L"Test");

            SetNextClockAccessTime_DurationTracker(now + 500ms);
            tracker.PauseTimers();

            SetNextClockAccessTime_DurationTracker(now + 10'000ms);
            tracker.ResumeTimers();

            SetNextClockAccessTime_DurationTracker(now + 11'000ms);
            tracker.ResumeTimers();

            SetNextClockAccessTime_DurationTracker(now + 10'000ms);
            auto measuredDuration = tracker.EndTimerFor(L"Test");

            Assert::AreEqual(500, (int)(*measuredDuration).count(), L"Duration between start & end was inaccurate");
        }

        TEST_METHOD(ResumingTrackerWithoutPausingNoImpact)
        {
            tracker.StartTimerFor(L"Test");

            SetNextClockAccessTime_DurationTracker(now + 10'000ms);
            tracker.ResumeTimers();

            SetNextClockAccessTime_DurationTracker(now + 10'000ms);
            auto measuredDuration = tracker.EndTimerFor(L"Test");

            Assert::AreEqual(10'000, (int)(*measuredDuration).count(), L"Duration between start & end was inaccurate");
        }
    };
} } }
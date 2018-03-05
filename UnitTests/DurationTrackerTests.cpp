#include "pch.h"
#include <chrono>
#include "CppUnitTest.h"
#include "DurationTracker.h"

using namespace Codevoid::Utilities::Mixpanel;

using namespace Platform;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace std::chrono;

namespace Codevoid { namespace Tests { namespace Mixpanel {
    TEST_CLASS(DurationTrackerTests)
    {
    public:
        TEST_METHOD(CanTrackTimeForOneEvent)
        {
            auto now = steady_clock::now();
            DurationTracker tracker(now);
            tracker.StartTimerFor(L"Test");

            tracker.__SetClock(now + 500ms);
            auto measuredDuration = tracker.EndTimerFor(L"Test");

            Assert::AreEqual(500, (int)(*measuredDuration).count(), L"Duration between start & end was inaccurate");
        }

        TEST_METHOD(EndingTimerForEventThatWasntStartedReturnsEmptyValue)
        {
            auto now = steady_clock::now();
            DurationTracker tracker(now);
            auto measuredDuration = tracker.EndTimerFor(L"Test");

            Assert::IsFalse(measuredDuration.has_value(), L"An event that didn't start, shouldn't have a value when ending");
        }

        TEST_METHOD(CanTrackTimeForTwoEvent)
        {
            auto now = steady_clock::now();
            DurationTracker tracker(now);
            tracker.StartTimerFor(L"Test");
            
            tracker.__SetClock(now);
            tracker.StartTimerFor(L"Test2");

            tracker.__SetClock(now + 500ms);
            auto measuredDuration = tracker.EndTimerFor(L"Test");
            Assert::AreEqual(500, (int)(*measuredDuration).count(), L"Duration between start & end was inaccurate");

            tracker.__SetClock(now + 750ms);
            measuredDuration = tracker.EndTimerFor(L"Test2");
            Assert::AreEqual(750, (int)(*measuredDuration).count(), L"Duration between start & end on second event was inaccurate");
        }

        TEST_METHOD(TimersAdjustedForPausedTime)
        {
            auto now = steady_clock::now();
            DurationTracker tracker(now);
            tracker.StartTimerFor(L"Test");

            tracker.__SetClock(now + 500ms);
            tracker.PauseTimers();

            tracker.__SetClock(now + 10'000ms);
            tracker.ResumeTimers();

            tracker.__SetClock(now + 10'000ms);
            auto measuredDuration = tracker.EndTimerFor(L"Test");

            Assert::AreEqual(500, (int)(*measuredDuration).count(), L"Duration between start & end was inaccurate");
        }

        TEST_METHOD(TimeIsntAdjustedWhenTrackingStartsWhilePaused)
        {
            auto now = steady_clock::now();
            DurationTracker tracker(now);

            tracker.__SetClock(now + 500ms);
            tracker.PauseTimers();

            tracker.__SetClock(now + 1'000ms);
            tracker.StartTimerFor(L"Test");

            tracker.__SetClock(now + 10'000ms);
            tracker.ResumeTimers();

            tracker.__SetClock(now + 10'000ms);
            auto measuredDuration = tracker.EndTimerFor(L"Test");

            Assert::AreEqual(9'000, (int)(*measuredDuration).count(), L"Duration between start & end was inaccurate");
        }

        TEST_METHOD(PausingTrackerMoreThanOnceHasNoImpact)
        {
            auto now = steady_clock::now();
            DurationTracker tracker(now);
            tracker.StartTimerFor(L"Test");

            tracker.__SetClock(now + 500ms);
            tracker.PauseTimers();

            tracker.__SetClock(now + 9'000ms);
            tracker.PauseTimers();

            tracker.__SetClock(now + 10'000ms);
            tracker.ResumeTimers();

            tracker.__SetClock(now + 10'000ms);
            auto measuredDuration = tracker.EndTimerFor(L"Test");

            Assert::AreEqual(500, (int)(*measuredDuration).count(), L"Duration between start & end was inaccurate");
        }

        TEST_METHOD(ResumingTrackerMoreThanOnceHasNoImpact)
        {
            auto now = steady_clock::now();
            DurationTracker tracker(now);
            tracker.StartTimerFor(L"Test");

            tracker.__SetClock(now + 500ms);
            tracker.PauseTimers();

            tracker.__SetClock(now + 10'000ms);
            tracker.ResumeTimers();

            tracker.__SetClock(now + 11'000ms);
            tracker.ResumeTimers();

            tracker.__SetClock(now + 10'000ms);
            auto measuredDuration = tracker.EndTimerFor(L"Test");

            Assert::AreEqual(500, (int)(*measuredDuration).count(), L"Duration between start & end was inaccurate");
        }

        TEST_METHOD(ResumingTrackerWithoutPausingNoImpact)
        {
            auto now = steady_clock::now();
            DurationTracker tracker(now);
            tracker.StartTimerFor(L"Test");

            tracker.__SetClock(now + 10'000ms);
            tracker.ResumeTimers();

            tracker.__SetClock(now + 10'000ms);
            auto measuredDuration = tracker.EndTimerFor(L"Test");

            Assert::AreEqual(10'000, (int)(*measuredDuration).count(), L"Duration between start & end was inaccurate");
        }
    };
} } }
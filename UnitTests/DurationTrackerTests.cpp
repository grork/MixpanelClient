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
            auto duration = tracker.EndTimerFor(L"Test");
            
            Assert::AreEqual(500, (int)duration.count(), L"Duration between start & end was in accurate");
        }

        TEST_METHOD(CanTrackTimeForTwoEvent)
        {
            auto now = steady_clock::now();
            DurationTracker tracker(now);
            tracker.StartTimerFor(L"Test");
            
            tracker.__SetClock(now);
            tracker.StartTimerFor(L"Test2");

            tracker.__SetClock(now + 500ms);
            auto duration = tracker.EndTimerFor(L"Test");
            Assert::AreEqual(500, (int)duration.count(), L"Duration between start & end was in accurate");

            tracker.__SetClock(now + 750ms);
            duration = tracker.EndTimerFor(L"Test2");
            Assert::AreEqual(750, (int)duration.count(), L"Duration between start & end was in accurate");
        }
    };
} } }
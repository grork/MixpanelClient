#include "pch.h"

#include "CppUnitTest.h"
#include "BackgroundWorker.h"

using namespace Platform;
using namespace std;
using namespace Codevoid::Utilities;
using namespace concurrency;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Windows::Data::Json;
using namespace Windows::Foundation::Collections;
using namespace Windows::Storage;

namespace Codevoid::Tests
{
    vector<shared_ptr<int>> processAll(const vector<shared_ptr<int>>& current, const function<bool()>& shouldKeepProcessing)
    {
        if (!shouldKeepProcessing)
        {
            return vector<shared_ptr<int>>();
        }

        return vector<shared_ptr<int>>(begin(current), end(current));
    }

    TEST_CLASS(BackgroundWorkerTests)
    {

    public:
        TEST_METHOD(CanInstantiateWorker)
        {
            BackgroundWorker<int> worker(
                bind(processAll, placeholders::_1, placeholders::_2),
                [](auto) { },
                L"CanInstantiateWorker");

            worker.AddWork(make_shared<int>(7));

            worker.Start();
        }

        TEST_METHOD(WorkIsDeqeuedAfterThresholdBeforeTimeout)
        {
            condition_variable workDequeued;
            mutex workMutex;
            unique_lock<mutex> workLock(workMutex);

            // We want this worker to wait 1000ms for the items to dequeue, or
            // when there is > 1 item in the queue. The 1000ms is there to allow
            // us to timeout
            BackgroundWorker<int> worker(bind(
                processAll, placeholders::_1, placeholders::_2),
                [&workDequeued](auto)
                {
                    workDequeued.notify_all();
                }, L"WorkIsDeqeuedAfterThresholdBeforeTimeout", 1000ms, 1);

            worker.Start();
            this_thread::sleep_for(100ms); // Wait for worker to be ready

            worker.AddWork(make_shared<int>(7));
            worker.AddWork(make_shared<int>(9));

            auto status = workDequeued.wait_for(workLock, 750ms, [&worker]() {
                return worker.GetQueueLength() == 0;
            });

            size_t queueLength = worker.GetQueueLength();
            worker.Shutdown();

            Assert::AreEqual(0, (int)queueLength, L"Items still in queue");
            Assert::IsTrue(status, L"Queue didn't reach 0 before timeout");
        }

        TEST_METHOD(BulkAddedWorkIsDeqeuedAfterThresholdBeforeTimeout)
        {
            condition_variable workDequeued;
            mutex workMutex;
            unique_lock<mutex> workLock(workMutex);

            // We want this worker to wait 1000ms for the items to dequeue, or
            // when there is > 1 item in the queue. The 1000ms is there to allow
            // us to timeout
            BackgroundWorker<int> worker(bind(
                processAll, placeholders::_1, placeholders::_2),
                [&workDequeued](auto)
            {
                workDequeued.notify_all();
            }, L"WorkIsDeqeuedAfterThresholdBeforeTimeout", 1000ms, 1);

            worker.Start();
            this_thread::sleep_for(100ms); // Wait for worker to be ready

            worker.AddWork({ make_shared<int>(7), make_shared<int>(9) });

            auto status = workDequeued.wait_for(workLock, 750ms, [&worker]() {
                return worker.GetQueueLength() == 0;
            });

            size_t queueLength = worker.GetQueueLength();
            worker.Shutdown();

            Assert::AreEqual(0, (int)queueLength, L"Items still in queue");
            Assert::IsTrue(status, L"Queue didn't reach 0 before timeout");
        }

        TEST_METHOD(WorkIsProcessedAfterQueueIsEmptiedAndItemsQueued)
        {
            condition_variable workDequeued;
            mutex workMutex;
            unique_lock<mutex> workLock(workMutex);

            // We want this worker to wait 1000ms for the items to dequeue, or
            // when there is > 1 item in the queue. The 1000ms is there to allow
            // us to timeout
            BackgroundWorker<int> worker(bind(
                processAll, placeholders::_1, placeholders::_2),
                [&workDequeued](auto)
            {
                workDequeued.notify_all();
            }, L"WorkIsProcessedAfterQueueIsEmptiedAndItemsQueued", 5000ms, 2);

            worker.Start();
            this_thread::sleep_for(100ms); // Wait for worker to be ready

            worker.AddWork(make_shared<int>(7));
            worker.AddWork(make_shared<int>(9));

            auto status = workDequeued.wait_for(workLock, 100ms, [&worker]() {
                return worker.GetQueueLength() == 0;
            });

            size_t queueLength = worker.GetQueueLength();

            Assert::AreEqual(0, (int)queueLength, L"Items still in queue");
            Assert::IsTrue(status, L"Queue didn't reach 0 before timeout");

            this_thread::sleep_for(700ms);

            worker.AddWork(make_shared<int>(10));
            worker.AddWork(make_shared<int>(11));

            status = workDequeued.wait_for(workLock, 100ms, [&worker]() {
                return worker.GetQueueLength() == 0;
            });

            queueLength = worker.GetQueueLength();

            Assert::AreEqual(0, (int)queueLength, L"Items still in queue");
            Assert::IsTrue(status, L"Queue didn't reach 0 before timeout");
        }

        TEST_METHOD(WorkIsDeqeuedAfterThresholdBeforeTimeoutWhenQueuedBeforeStarting)
        {
            condition_variable workDequeued;
            mutex workMutex;
            unique_lock<mutex> workLock(workMutex);

            // We want this worker to wait 1000ms for the items to dequeue, or
            // when there is > 1 item in the queue. The 1000ms is there to allow
            // us to timeout
            BackgroundWorker<int> worker(
                bind(processAll, placeholders::_1, placeholders::_2),
                [&workDequeued](auto)
                {
                    workDequeued.notify_all();
                }, L"WorkIsDeqeuedAfterThresholdBeforeTimeoutWhenQueuedBeforeStarting", 1000ms, 2);

            worker.AddWork(make_shared<int>(7));
            worker.AddWork(make_shared<int>(9));
            worker.AddWork(make_shared<int>(11));

            worker.Start();
            this_thread::sleep_for(100ms); // Wait for worker to be ready

            auto status = workDequeued.wait_for(workLock, 750ms, [&worker]() {
                return worker.GetQueueLength() == 0;
            });

            size_t queueLength = worker.GetQueueLength();
            worker.Shutdown();

            Assert::AreEqual(0, (int)queueLength, L"Items still in queue");
            Assert::IsTrue(status, L"Queue didn't reach 0 before timeout");
        }

        TEST_METHOD(WorkIsDeqeuedAfterTimeoutBeforeThreshold)
        {
            condition_variable workDequeued;
            mutex workMutex;
            unique_lock<mutex> workLock(workMutex);

            // Setting the threshold higher than the number we queue
            // but the timeout to something low so we can wait
            // to sure that the timeout is the one triggering not, the threshold.
            BackgroundWorker<int> worker(
                bind(processAll, placeholders::_1, placeholders::_2),
                [&workDequeued](auto)
                {
                    workDequeued.notify_all();
                }, L"WorkIsDeqeuedAfterTimeoutBeforeThreshold", 200ms, 10);

            worker.Start();
            this_thread::sleep_for(100ms); // Wait for worker to be ready

            worker.AddWork(make_shared<int>(7));
            worker.AddWork(make_shared<int>(9));

            auto status = workDequeued.wait_for(workLock, 500ms, [&worker]() {
                return worker.GetQueueLength() == 0;
            });

            size_t queueLength = worker.GetQueueLength();
            worker.Shutdown();

            Assert::AreEqual(0, (int)queueLength, L"Items still in queue");
            Assert::IsTrue(status, L"Queue didn't reach 0 before timeout");
        }

        TEST_METHOD(WorkIsDeqeuedOnShutdownDrainBeforeTimeoutOrThreshold)
        {
            bool postProcessCalled = false;

            BackgroundWorker<int> worker(
                bind(processAll, placeholders::_1, placeholders::_2),
                [&postProcessCalled] (auto)
                {
                    postProcessCalled = true;
                },
                L"WorkIsDeqeuedOnShutdownDrainBeforeTimeoutOrThreshold", 1000ms, 10);

            worker.Start();
            this_thread::sleep_for(100ms); // Wait for worker to be ready

            worker.AddWork(make_shared<int>(7));
            worker.AddWork(make_shared<int>(9));

            worker.Shutdown();

            Assert::AreEqual(0, (int)worker.GetQueueLength(), L"Items still in queue");
            Assert::IsFalse(postProcessCalled, L"Queue was drained, but post process shouldn't have been called");
        }

        TEST_METHOD(WorkIsProcessedButNotPostProcessedWhenDrained)
        {
            bool postProcessCalled = false;
            bool processWasCalled = false;

            BackgroundWorker<int> worker(
                [&processWasCalled](auto current, auto shouldKeepProcessing)
                {
                    processWasCalled = true;
                    return processAll(current, shouldKeepProcessing);
                },
                [&postProcessCalled](auto)
                {
                    postProcessCalled = true;
                },
                L"WorkIsProcessedButNotPostProcessedWhenDrained", 1000ms, 10);

            worker.Start();
            this_thread::sleep_for(100ms); // Wait for worker to be ready

            worker.AddWork(make_shared<int>(7));
            worker.AddWork(make_shared<int>(9));

            worker.Shutdown();

            Assert::AreEqual(0, (int)worker.GetQueueLength(), L"Items still in queue");
            Assert::IsTrue(processWasCalled, L"Queue was drained, but nothing was processed");
            Assert::IsFalse(postProcessCalled, L"Queue was drained, but post process shouldn't have been called");
        }

        TEST_METHOD(WorkIsNotProcessedAndNotPostProcessedWhenDropped)
        {
            bool postProcessCalled = false;
            bool processWasCalled = false;

            BackgroundWorker<int> worker(
                [&processWasCalled](auto current, auto shouldKeepProcessing)
            {
                if (!shouldKeepProcessing)
                {
                    return vector<shared_ptr<int>>();
                }

                processWasCalled = true;
                return processAll(current, shouldKeepProcessing);
            },
                [&postProcessCalled](auto)
            {
                postProcessCalled = true;
            },
                L"WorkIsNotProcessedAndNotPostProcessedWhenDropped", 1000ms, 10);

            worker.Start();
            this_thread::sleep_for(100ms); // Wait for worker to be ready

            worker.AddWork(make_shared<int>(7));
            worker.AddWork(make_shared<int>(9));

            worker.ShutdownAndDrop();

            Assert::AreEqual(2, (int)worker.GetQueueLength(), L"Items still in queue");
            Assert::IsFalse(processWasCalled, L"Queue was drained, but something was processed");
            Assert::IsFalse(postProcessCalled, L"Queue was drained, but post process shouldn't have been called");
        }

        TEST_METHOD(WorkStopsProcessingWhileProcessingCurrentWork)
        {
            int postProcessItemCount = 0;
            bool processWasCalled = false;

            BackgroundWorker<int> worker(
                [&processWasCalled](auto current, auto shouldKeepProcessing)
                {
                    vector<shared_ptr<int>> items;
                    processWasCalled = true;
                    for (auto&& item : current)
                    {
                        if (!shouldKeepProcessing())
                        {
                            break;
                        }

                        items.emplace_back(item);
                        this_thread::sleep_for(500ms);
                    }

                    return items;
                },
                [&postProcessItemCount](auto items)
                {
                    postProcessItemCount += (int)items.size();
                },
                L"WorkStopsProcessingWhileProcessingCurrentWork", 1000ms, 4);

            worker.Start();
            this_thread::sleep_for(100ms); // Wait for worker to be ready

            // Queue 4 times to trigger the dequeue
            worker.AddWork(make_shared<int>(7));
            worker.AddWork(make_shared<int>(8));
            worker.AddWork(make_shared<int>(9));
            worker.AddWork(make_shared<int>(10));

            // Wait a little bit so we start processing the
            // first item back
            this_thread::sleep_for(50ms);

            // queue another one
            worker.AddWork(make_shared<int>(11));

            this_thread::sleep_for(650ms);

            worker.Shutdown();

            Assert::IsTrue(processWasCalled, L"Queue processed something");
            Assert::AreEqual(0, (int)worker.GetQueueLength(), L"Items still in queue");
            Assert::AreEqual(0, postProcessItemCount, L"Queue was drained, but post process wasn't called for the correct number of item");
        }

        TEST_METHOD(WorkCorrectlyDropsAfterStartingToProcessItems)
        {
            int postProcessItemCount = 0;
            int processedItemCount = 0;

            BackgroundWorker<int> worker(
                [&processedItemCount](auto current, auto shouldKeepProcessing)
                {
                    vector<shared_ptr<int>> items;
                    for (auto&& item : current)
                    {
                        if (!shouldKeepProcessing())
                        {
                            break;
                        }

                        items.emplace_back(item);
                        this_thread::sleep_for(500ms);
                    }

                    processedItemCount += (int)items.size();
                    return items;
                },
                [&postProcessItemCount](auto items)
                {
                    postProcessItemCount += (int)items.size();
                },
                L"WorkCorrectlyDropsAfterStartingToProcessItems", 1000ms, 4);

            worker.Start();
            this_thread::sleep_for(100ms); // Wait for worker to be ready

            // Queue 4 times to trigger the dequeue
            worker.AddWork(make_shared<int>(7));
            worker.AddWork(make_shared<int>(8));
            worker.AddWork(make_shared<int>(9));
            worker.AddWork(make_shared<int>(10));

            // Wait a little bit so we start processing the
            // first item back
            this_thread::sleep_for(50ms);

            // queue another one
            worker.AddWork(make_shared<int>(11));

            this_thread::sleep_for(650ms);

            worker.ShutdownAndDrop();

            Assert::AreEqual(3, (int)worker.GetQueueLength(), L"Items still in queue");
            Assert::AreEqual(2, processedItemCount, L"Incorrect number of items processed before being asked to drop");
            Assert::AreEqual(0, postProcessItemCount, L"Queue was drained, but post process wasn't called for the correct number of item");
        }

        TEST_METHOD(WorkRemainsUnchangedAfterPausing)
        {
            bool postProcessCalled = false;

            // We want this worker to wait 1000ms for the items to dequeue, or
            // when there is > 1 item in the queue. The 1000ms is there to allow
            // us to timeout
            BackgroundWorker<int> worker(
                bind(processAll, placeholders::_1, placeholders::_2),
                [&postProcessCalled](auto)
                {
                    postProcessCalled = true;
                },
                L"WorkRemainsUnchangedAfterPausing", 200ms, 10);

            worker.Start();
            this_thread::sleep_for(100ms); // Wait for worker to be ready

            worker.AddWork(make_shared<int>(7));
            worker.AddWork(make_shared<int>(9));

            worker.Pause();

            this_thread::sleep_for(250ms);

            Assert::AreEqual(2, (int)worker.GetQueueLength(), L"Expected items in the queue");
            Assert::IsFalse(postProcessCalled, L"Queue was drained, but post process shouldn't have been called");
        }

        TEST_METHOD(WorkRemainsUnchangedAfterPausingTwice)
        {
            bool postProcessCalled = false;

            // We want this worker to wait 1000ms for the items to dequeue, or
            // when there is > 1 item in the queue. The 1000ms is there to allow
            // us to timeout
            BackgroundWorker<int> worker(
                bind(processAll, placeholders::_1, placeholders::_2),
                [&postProcessCalled](auto)
            {
                postProcessCalled = true;
            },
                L"WorkRemainsUnchangedAfterPausingTwice", 200ms, 10);

            worker.Start();
            this_thread::sleep_for(100ms); // Wait for worker to be ready

            worker.AddWork(make_shared<int>(7));
            worker.AddWork(make_shared<int>(9));

            worker.Pause();

            this_thread::sleep_for(250ms);

            Assert::AreEqual(2, (int)worker.GetQueueLength(), L"Expected items in the queue");
            Assert::IsFalse(postProcessCalled, L"Queue was drained, but post process shouldn't have been called");

            worker.Pause();

            this_thread::sleep_for(250ms);

            Assert::AreEqual(2, (int)worker.GetQueueLength(), L"Expected items in the queue");
            Assert::IsFalse(postProcessCalled, L"Queue was drained, but post process shouldn't have been called");
        }

        TEST_METHOD(WorkRemainsUnchangedAndPausedAfterPausingAndAddingWork)
        {
            bool postProcessCalled = false;

            BackgroundWorker<int> worker(
                bind(processAll, placeholders::_1, placeholders::_2),
                [&postProcessCalled](auto)
                {
                    postProcessCalled = true;
                },
                L"WorkRemainsUnchangedAndPausedAfterPausingAndAddingWork", 200ms, 3);

            worker.Start();
            this_thread::sleep_for(100ms); // Wait for worker to be ready

            worker.AddWork(make_shared<int>(7));
            worker.AddWork(make_shared<int>(9));

            worker.Pause();

            this_thread::sleep_for(200ms);

            Assert::AreEqual(2, (int)worker.GetQueueLength(), L"Expected items in the queue after pausing");
            Assert::IsFalse(postProcessCalled, L"Queue was drained, but post process shouldn't have been called");

            worker.AddWork(make_shared<int>(10));

            this_thread::sleep_for(250ms);

            Assert::AreEqual(3, (int)worker.GetQueueLength(), L"Expected items in the queue after adding while paused");
            Assert::IsFalse(postProcessCalled, L"Queue was drained, but post process shouldn't have been called");
        }

        TEST_METHOD(WorkProcessedAfterResumingFromPausedState)
        {
            bool postProcessCalled = false;

            BackgroundWorker<int> worker(
                bind(processAll, placeholders::_1, placeholders::_2),
                [&postProcessCalled](auto)
                {
                    postProcessCalled = true;
                },
                L"WorkProcessedAfterResumingFromPausedState", 200ms, 10);

            worker.Start();
            this_thread::sleep_for(100ms); // Wait for worker to be ready

            worker.AddWork(make_shared<int>(7));
            worker.AddWork(make_shared<int>(9));

            worker.Pause();

            this_thread::sleep_for(200ms);

            Assert::AreEqual(2, (int)worker.GetQueueLength(), L"Expected items in the queue");
            Assert::IsFalse(postProcessCalled, L"Queue was drained, but post process shouldn't have been called");

            worker.Start();

            this_thread::sleep_for(250ms);

            Assert::AreEqual(0, (int)worker.GetQueueLength(), L"Items still in queue");
            Assert::IsTrue(postProcessCalled, L"Queue was processed, but post process should have been called");
        }

        TEST_METHOD(WorkProcessedAfterShuttingDownFromPausedState)
        {
            bool postProcessCalled = false;

            BackgroundWorker<int> worker(
                bind(processAll, placeholders::_1, placeholders::_2),
                [&postProcessCalled](auto)
                {
                    postProcessCalled = true;
                },
                L"WorkProcessedAfterShuttingDownFromPausedState", 200ms, 10);

            worker.Start();
            this_thread::sleep_for(100ms); // Wait for worker to be ready

            worker.AddWork(make_shared<int>(7));
            worker.AddWork(make_shared<int>(9));

            worker.Pause();

            this_thread::sleep_for(200ms);

            Assert::AreEqual(2, (int)worker.GetQueueLength(), L"Expected items in the queue");
            Assert::IsFalse(postProcessCalled, L"Queue was drained, but post process shouldn't have been called");

            worker.Shutdown();

            Assert::AreEqual(0, (int)worker.GetQueueLength(), L"Items still in queue");
            Assert::IsFalse(postProcessCalled, L"Queue was processed, but post process should't have been called");
        }

        TEST_METHOD(WorkerIsNotTriggeredWhenOnlyQueueingNonCriticalWork)
        {
            condition_variable workDequeued;
            mutex workMutex;
            unique_lock<mutex> workLock(workMutex);

            // We want this worker to wait 1000ms for the items to dequeue, or
            // when there is > 1 item in the queue. The 1000ms is there to allow
            // us to timeout
            BackgroundWorker<int> worker(bind(
                processAll, placeholders::_1, placeholders::_2),
                [&workDequeued](auto)
            {
                workDequeued.notify_all();
            }, L"WorkIsDeqeuedAfterThresholdBeforeTimeout", 50ms, 3);

            worker.Start();
            this_thread::sleep_for(100ms); // Wait for worker to be ready

            worker.AddWork(make_shared<int>(7), WorkPriority::Low);
            worker.AddWork(make_shared<int>(8), WorkPriority::Low);
            worker.AddWork(make_shared<int>(9), WorkPriority::Low);
            worker.AddWork(make_shared<int>(10), WorkPriority::Low);
            worker.AddWork(make_shared<int>(11), WorkPriority::Low);

            auto status = workDequeued.wait_for(workLock, 100ms, [&worker]() {
                return worker.GetQueueLength() == 0;
            });

            size_t queueLength = worker.GetQueueLength();
            worker.Shutdown();

            Assert::AreEqual(5, (int)queueLength, L"Items still in queue");
            Assert::IsFalse(status, L"Queue didn't reach 0 before timeout");
        }
    };
}
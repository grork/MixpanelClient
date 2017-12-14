#include "pch.h"

#include "CppUnitTest.h"
#include "BackgroundWorker.h"

using namespace Platform;
using namespace std;
using namespace CodevoidN::Utilities::Mixpanel;
using namespace concurrency;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Windows::Data::Json;
using namespace Windows::Foundation::Collections;
using namespace Windows::Storage;

const wchar_t* TRACE_PREFIX = L"BackgroundWorkerTest";

namespace CodevoidN { namespace  Tests
{
    vector<shared_ptr<int>> processAll(const vector<shared_ptr<int>>& current, const WorkerState&)
    {
        return vector<shared_ptr<int>>(current.begin(), current.end());
    }

    TEST_CLASS(BackgroundWorkerTests)
    {

    public:
        TEST_METHOD(CanInstantiateWorker)
        {
            BackgroundWorker<int> worker(bind(processAll, placeholders::_1, placeholders::_2), [](auto) { }, TRACE_PREFIX);

            auto f = make_shared<int>(7);
            worker.AddWork(f);
        }

        TEST_METHOD(WorkIsDeqeuedAfterThresholdBeforeTimeout)
        {
            condition_variable workDequeued;
            mutex workMutex;
            unique_lock<mutex> workLock(workMutex);

            // We want this worker to wait 1000ms for the items to dequeue, or
            // when there is > 1 item in the queue. The 1000ms is there to allow
            // us to timeout
            BackgroundWorker<int> worker(bind(processAll, placeholders::_1, placeholders::_2),
                [&workDequeued](auto) {
                workDequeued.notify_all();
            }, TRACE_PREFIX, 1000ms, 1);

            worker.Start();
            this_thread::sleep_for(100ms); // Wait for worker to be ready

            worker.AddWork(make_shared<int>(7));
            worker.AddWork(make_shared<int>(9));

            auto status = workDequeued.wait_for(workLock, 750ms, [&worker]() {
                return worker.GetQueueLength() == 0;
            });

            size_t queueLength = worker.GetQueueLength();
            worker.Shutdown(WorkerState::Shutdown);

            Assert::AreEqual(0, (int)queueLength, L"Items still in queue");
            Assert::IsTrue(status, L"Queue didn't reach 0 before timeout");
        }

        TEST_METHOD(WorkIsDeqeuedAfterTimeoutBeforeThreshold)
        {
            condition_variable workDequeued;
            mutex workMutex;
            unique_lock<mutex> workLock(workMutex);

            // We want this worker to wait 1000ms for the items to dequeue, or
            // when there is > 1 item in the queue. The 1000ms is there to allow
            // us to timeout
            BackgroundWorker<int> worker(bind(processAll, placeholders::_1, placeholders::_2),
                [&workDequeued](auto) {
                workDequeued.notify_all();
            }, TRACE_PREFIX, 200ms, 10);

            worker.Start();
            this_thread::sleep_for(100ms); // Wait for worker to be ready

            worker.AddWork(make_shared<int>(7));
            worker.AddWork(make_shared<int>(9));

            auto status = workDequeued.wait_for(workLock, 500ms, [&worker]() {
                return worker.GetQueueLength() == 0;
            });

            size_t queueLength = worker.GetQueueLength();
            worker.Shutdown(WorkerState::Shutdown);

            Assert::AreEqual(0, (int)queueLength, L"Items still in queue");
            Assert::IsTrue(status, L"Queue didn't reach 0 before timeout");
        }

        TEST_METHOD(WorkIsDeqeuedOnShutdownDrainBeforeTimeoutOrThreshold)
        {
            bool postProcessCalled = false;

            // We want this worker to wait 1000ms for the items to dequeue, or
            // when there is > 1 item in the queue. The 1000ms is there to allow
            // us to timeout
            BackgroundWorker<int> worker(
                bind(processAll, placeholders::_1, placeholders::_2),
                [&postProcessCalled] (auto)
                {
                    postProcessCalled = true;
                },
                TRACE_PREFIX, 1000ms, 10);

            worker.Start();
            this_thread::sleep_for(100ms); // Wait for worker to be ready

            worker.AddWork(make_shared<int>(7));
            worker.AddWork(make_shared<int>(9));

            worker.Shutdown(WorkerState::Drain);

            Assert::AreEqual(0, (int)worker.GetQueueLength(), L"Items still in queue");
            Assert::IsFalse(postProcessCalled, L"Queue was drained, but post process shouldn't have been called");
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
                TRACE_PREFIX, 200ms, 10);

            worker.Start();
            this_thread::sleep_for(100ms); // Wait for worker to be ready

            worker.AddWork(make_shared<int>(7));
            worker.AddWork(make_shared<int>(9));

            worker.Pause();

            this_thread::sleep_for(200ms);

            Assert::AreEqual(2, (int)worker.GetQueueLength(), L"Items still in queue");
            Assert::IsFalse(postProcessCalled, L"Queue was drained, but post process shouldn't have been called");
        }
    };
} }
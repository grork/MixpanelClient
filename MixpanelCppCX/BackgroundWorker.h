#pragma once

#include <agents.h>
#include "Tracing.h"

namespace CodevoidN { namespace Utilities { namespace Mixpanel {
    template <typename ItemType>
    class BackgroundWorker
    {
        using ItemType_ptr = std::shared_ptr<ItemType>;
        using ItemTypeVector = std::vector<ItemType_ptr>;
        using lock_guard_mutex = std::lock_guard<std::mutex>;

    public:
        BackgroundWorker(
            std::function<ItemTypeVector(ItemTypeVector&, const std::function<bool()>&)> processItemsCallback,
            std::function<void(ItemTypeVector&)> postProcessItemsCallback,
            const std::wstring& tracePrefix,
            const std::chrono::milliseconds debounceTimeout = std::chrono::milliseconds(500),
            size_t debounceItemThreshold = 10
        ) :
            m_processItemsCallback(processItemsCallback),
            m_postProcessItemsCallback(postProcessItemsCallback),
            m_tracePrefix(tracePrefix),
            m_workerStarted(false),
            m_state(WorkerState::None),
            m_debounceTimeout(debounceTimeout),
            m_debounceItemThreshold(debounceItemThreshold)
        { }

        ~BackgroundWorker()
        {
            TRACE_OUT(m_tracePrefix + L": Queue being destroyed");
            this->Shutdown(WorkerState::Drain);
            TRACE_OUT(m_tracePrefix + L": Queue Destroyed");
        }

        size_t GetQueueLength()
        {
            lock_guard_mutex lock(m_accessLock);
            return m_items.size();
        }

        void AddWork(ItemType_ptr item)
        {
            TRACE_OUT(m_tracePrefix + L": Adding Item");

            {
                lock_guard_mutex lock(m_accessLock);
                m_items.emplace_back(item);
            }

            TRACE_OUT(m_tracePrefix + L": Notifying worker thread");

            this->TriggerWorkOrWaitForIdle();
        }

        void TriggerWorkOrWaitForIdle()
        {
            if (!m_workerStarted)
            {
                TRACE_OUT(m_tracePrefix + L": Skipping triggering worker, since it's not started");
                return;
            }

            CancelConcurrencyTimer(m_debounceTimer, m_debounceTimerCallback);

            if (this->GetQueueLength() >= m_debounceItemThreshold)
            {
                m_hasItems.notify_one();
            }
            else
            {
                auto timer = CreateConcurrencyTimer(m_debounceTimeout, [this]() {
                    TRACE_OUT(m_tracePrefix + L": Debounce Timer tiggered");
                    m_hasItems.notify_one();
                });

                m_debounceTimer = std::get<0>(timer);
                m_debounceTimerCallback = std::get<1>(timer);

                m_debounceTimer->start();
            }
        }

        void Start()
        {
            if (m_processItemsCallback == nullptr)
            {
                throw std::invalid_argument("Must provide a ProcessItems function start worker");
            }

            if (m_postProcessItemsCallback == nullptr)
            {
                throw std::invalid_argument("Must provide a PostProcessItems function start worker");
            }

            if (m_workerStarted)
            {
                TRACE_OUT(m_tracePrefix + L"Write To Storage worker already running");
                return;
            }

            TRACE_OUT(m_tracePrefix + L": Starting Write To Storage worker");
            m_workerThread = std::thread(&BackgroundWorker::Worker, this);
            this->TriggerWorkOrWaitForIdle();
        }

        void Clear()
        {
            TRACE_OUT(m_tracePrefix + L": Clearing");
            lock_guard_mutex lock(m_accessLock);
            m_items.clear();
            TRACE_OUT(m_tracePrefix + L": Cleared");
        }

        void Pause()
        {
            TRACE_OUT(m_tracePrefix + L": Trying To pause Worker");
            this->Shutdown(WorkerState::Paused);
        }

        void Shutdown()
        {
            this->Shutdown(WorkerState::Drain);
        }

        void ShutdownAndDrop()
        {
            this->Shutdown(WorkerState::Drop);
        }

        void SetDebounceTimeout(std::chrono::milliseconds debounceTimeout)
        {
            if (m_workerStarted)
            {
                throw std::logic_error("Cannot change debounce timeout while worker is running");
            }

            m_debounceTimeout = debounceTimeout;
        }

        void SetDebounceItemThreshold(size_t debounceItemThreshold)
        {
            if (m_workerStarted)
            {
                throw std::logic_error("Cannot change debounce item threshold while worker is running");
            }

            m_debounceItemThreshold = debounceItemThreshold;
        }

    private:
        enum class WorkerState
        {
            None,
            Paused,
            Drain,
            Drop,
            Shutdown
        };

        // concurrency::timer is weird, and requires you to set things up in weird ways
        // (hence the pointers). Also, it can't be "Restarted" if it's a single timer
        // so we need to recreate it every time. This function just wraps things up neatly.
        std::tuple<std::shared_ptr<Concurrency::timer<int>>, std::shared_ptr<Concurrency::call<int>>> CreateConcurrencyTimer(std::chrono::milliseconds timeout, std::function<void()> callback)
        {
            auto callWrapper = std::make_shared<Concurrency::call<int>>([callback](int) {
                callback();
            });

            auto timerInstance = std::make_shared<Concurrency::timer<int>>((int)timeout.count(), 0, callWrapper.get(), false);

            return make_tuple(timerInstance, callWrapper);
        }

        // Per CreateConcurrencyTimer, this helps clean the timers up.
        static void CancelConcurrencyTimer(std::shared_ptr<Concurrency::timer<int>>& timer, std::shared_ptr<Concurrency::call<int>>& target)
        {
            if (timer != nullptr && target != nullptr)
            {
                timer->stop();
                timer->unlink_target(target.get());
            }

            target = nullptr;
            timer = nullptr;
        }

        bool ShouldKeepProcessingItems()
        {
            return (m_state < WorkerState::Drop);
        }

        void Shutdown(const WorkerState state)
        {
            WorkerState previousState = m_state;
            m_state = state;

            TRACE_OUT(m_tracePrefix + L": Shutting down");
            if (!m_workerStarted && (previousState != WorkerState::Paused))
            {
                TRACE_OUT(m_tracePrefix + L": Not actually started");
                return;
            }

            if ((previousState == WorkerState::Paused) && (state != WorkerState::Paused) && (this->GetQueueLength() > 0))
            {
                TRACE_OUT(m_tracePrefix + L": Worker was paused, starting again to allow draining");
                this->Start();
            }

            CancelConcurrencyTimer(m_debounceTimer, m_debounceTimerCallback);

            if (m_workerThread.joinable())
            {
                TRACE_OUT(m_tracePrefix + L": Waiting on Worker Thread");
                m_state = state;
                m_hasItems.notify_all();
                m_workerThread.join();
                assert(!m_workerStarted);
            }

            TRACE_OUT(m_tracePrefix + L": Shutdown");
        }

        void Worker()
        {
            // When we make the first iteration through the loops
            // we will want to wait if there aren't enough items in
            // the queue yet.
            bool waitForFirstWakeUp = this->GetQueueLength() <= m_debounceItemThreshold;
            m_state = WorkerState::None;
            m_workerStarted = true;

            while (m_state < WorkerState::Shutdown)
            {
                TRACE_OUT(m_tracePrefix + L": Worker Starting Loop Iteration");
                ItemTypeVector itemsToPersist;

                {
                    std::unique_lock<std::mutex> lock(m_accessLock);

                    // If we don't have anything in the queue, lets wait for something
                    // to be in it, or to shutdown.
                    if (m_items.size() < 1)
                    {
                        TRACE_OUT(m_tracePrefix + L": Waiting for Items to process");
                        m_hasItems.wait(lock, [this, &waitForFirstWakeUp]() {
                            TRACE_OUT(m_tracePrefix + L": Condition Triggered. State: " + to_wstring((int)m_state.load()));

                            // If we're going away, we can ignore all other state
                            // and allow the thread to continue and eventually
                            // shutdown.
                            if (m_state > WorkerState::None)
                            {   
                                return true;
                            }

                            // During the first iteration of the loop when started, we
                            // might have items, but not enough to wake up the thread
                            // normally. But since this is the first pass, we might have
                            // fewer than that number. Ties to the conditional check on
                            // the wait below, we'll start pushing items even though our
                            // thresholds / idle timeout hasn't been hit.
                            //
                            // If we don't have enough items on the first pass, we'll
                            // just wait until our second wake up -- assumed to be triggered
                            // by some external force (timer, threshold), and process those
                            // items normally.
                            if (waitForFirstWakeUp)
                            {
                                waitForFirstWakeUp = false;
                                return false;
                            }

                            // Only wake up if we actually have some times to process
                            return (m_items.size() > 0);
                        });
                    }

                    // If we've been asked to pause, just give up on everything, and
                    // leave the queue, and state as is.
                    if (m_state == WorkerState::Paused || m_state == WorkerState::Drop)
                    {
                        break;
                    }

                    itemsToPersist.assign(begin(m_items), end(m_items));
                }

                // When we've got no items, and we're shuting down, theres
                // no work for us to do (no items), so we're just going to
                // break out of the loop right now, and let the clean up happen
                if ((itemsToPersist.size() == 0) && (m_state > WorkerState::Paused))
                {
                    TRACE_OUT(m_tracePrefix + L": No items, exiting loop");
                    break;
                }

                // Assume we should have some items if we've gotten this far
                assert(itemsToPersist.size() > 0);

                TRACE_OUT(m_tracePrefix + L": Processing Items");
                ItemTypeVector successfullyProcessed =
                    this->m_processItemsCallback(itemsToPersist, bind(&BackgroundWorker<ItemType>::ShouldKeepProcessingItems, this));

                // Remove the items from the queue
                {
                    lock_guard_mutex lock(m_accessLock);

                    TRACE_OUT(m_tracePrefix + L": Clearing Queue of processed items");

                    // Remove the items from the list that had been successfully processed
                    // This requires us to loop through the list multiple times (boo), since
                    // we're changing the list, and they may be non-contiguous. Hopefully,
                    // however, the items are near the front of the list since thats what we
                    // were processing.
                    for (auto&& processedItem : successfullyProcessed)
                    {
                        auto removeAt = find(begin(m_items),
                            end(m_items),
                            processedItem);

                        if (removeAt == m_items.end())
                        {
                            // Didn't find the item, so it must not be in the list any more
                            continue;
                        }

                        m_items.erase(removeAt);
                    }
                }

                if (m_state > WorkerState::Paused)
                {
                    TRACE_OUT(m_tracePrefix + L": Queue shutting down, skipping post processing");
                    continue;
                }

                TRACE_OUT(m_tracePrefix + L": Post Processing");
                m_postProcessItemsCallback(successfullyProcessed);
            }

            m_workerStarted = false;
            m_state = (m_state == WorkerState::Paused) ? WorkerState::Paused : WorkerState::Shutdown;
        }

        std::shared_ptr<concurrency::timer<int>> m_debounceTimer;
        std::shared_ptr<concurrency::call<int>> m_debounceTimerCallback;
        std::chrono::milliseconds m_debounceTimeout;
        size_t m_debounceItemThreshold;
        std::function<ItemTypeVector(ItemTypeVector&, const std::function<bool()>&)> m_processItemsCallback;
        std::function<void(ItemTypeVector&)> m_postProcessItemsCallback;
        std::wstring m_tracePrefix;
        std::vector<ItemType_ptr> m_items;
        std::mutex m_accessLock;
        std::condition_variable m_hasItems;
        std::atomic<bool> m_workerStarted;
        std::thread m_workerThread;
        std::atomic<WorkerState> m_state;
    };
} } }
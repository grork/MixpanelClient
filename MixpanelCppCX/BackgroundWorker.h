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
        /// <summary>
        /// Creates, but does not start, a worker queue that is processed on
        /// a background thread.
        ///
        /// <param name='processItemsCallback'>
        /// Handler to process the actual item when required. This should do
        /// the work of actually handling the items in the queue.
        /// </param>
        /// <param name='postProcessItemsCallback'>
        /// When an item has been successfully processed, this can be used to perform
        /// additional processing on those items after they've been removed from the
        /// main queue. The primary use case is to allow them to be placed into another
        /// queue for additional processing
        /// </param>
        /// <param name='tracePrefix'>
        /// When built for 'Debugging', this is prefixed to all all trace logging.
        /// Intended to help with diagnostics.
        /// </param>
        /// <param name='idleTimeout'>
        /// Duration to wait for idle before processing items in the queue.
        /// </param>
        /// <param name='itemThreshold'>
        /// Number of items to wait for before processing the queue, irrespective
        /// of the idle time out. E.g. If you're adding events rapidly, you'll never
        /// reach the idle timeout, so you'll want to start processing items.
        /// </param>
        /// </summary>
        BackgroundWorker(
            std::function<ItemTypeVector(ItemTypeVector&, const std::function<bool()>&)> processItemsCallback,
            std::function<void(ItemTypeVector&)> postProcessItemsCallback,
            const std::wstring& tracePrefix,
            const std::chrono::milliseconds idleTimeout = std::chrono::milliseconds(500),
            size_t itemThreshold = 10
        ) :
            m_processItemsCallback(processItemsCallback),
            m_postProcessItemsCallback(postProcessItemsCallback),
            m_tracePrefix(tracePrefix),
            m_state(WorkerState::None),
            m_idleTimeout(idleTimeout),
            m_itemThreshold(itemThreshold)
        { }

        ~BackgroundWorker()
        {
            TRACE_OUT(m_tracePrefix + L": Queue being destroyed");
            this->Shutdown(WorkerState::Drain);
            TRACE_OUT(m_tracePrefix + L": Queue Destroyed");
        }

        size_t GetQueueLength()
        {
            lock_guard_mutex lock(m_itemsLock);
            return m_items.size();
        }

        /// <summary>
        /// Adds the supplied work to the queue for later processing.
        /// Will reset the idle timer or start processing immediately if
        /// the queue length has reached the supplied limit.
        ///
        /// If the worker isn't started, items are just placed in the queue,
        /// and will be processed once the worker has been started.
        /// </summary>
        void AddWork(const ItemType_ptr item)
        {
            TRACE_OUT(m_tracePrefix + L": Adding Item");

            {
                lock_guard_mutex lock(m_itemsLock);
                m_items.emplace_back(item);
            }

            TRACE_OUT(m_tracePrefix + L": Notifying worker thread");

            this->TriggerWorkOrWaitForIdle();
        }

        /// <summary>
        /// Starts the background processing inline with the idle timeout & item
        /// limits. Will keep running until paused, shutdown, or instance is cleanedup
        /// </summary>
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

            if (m_state == WorkerState::Running)
            {
                TRACE_OUT(m_tracePrefix + L"Write To Storage worker already running");
                return;
            }

            TRACE_OUT(m_tracePrefix + L": Starting worker");
            {
                std::unique_lock<mutex> lock(m_workerLock);
                m_workerThread = std::thread(&BackgroundWorker::Worker, this);

                TRACE_OUT(m_tracePrefix + L"Waiting to be notified worker has succesfully started");
                m_hasWorkerStarted.wait(lock);
                assert(m_state == WorkerState::Running);
            }

            this->TriggerWorkOrWaitForIdle();
        }

        /// <summary>
        /// Removes any items that are currently in the queue, even if they're
        /// currently being processed.
        /// </summary>
        void Clear()
        {
            TRACE_OUT(m_tracePrefix + L": Clearing");
            lock_guard_mutex lock(m_itemsLock);
            m_items.clear();
            TRACE_OUT(m_tracePrefix + L": Cleared");
        }

        /// <summary>
        /// Stops the worker from processing any more items after it's finished it's current batch.
        /// This is intended to keep items in memory, assuming they'll be processed later.
        /// 
        /// Blocks the current thread until the queue has successfully paused.
        ///
        /// Note, if you pause and then destroy the queue without resuming the worker, the will
        /// be started to process all the items as quickly as possible, but don't post-process
        /// any of the items.
        /// </summary>
        void Pause()
        {
            TRACE_OUT(m_tracePrefix + L": Trying To pause Worker");
            this->Shutdown(WorkerState::Paused);
        }

        /// <summary>
        /// Waits for worker to process (but not post process) all items currently in the queue.
        ///
        /// Blocks the current thread until the queue has successfully processed all items
        /// </summary>
        void Shutdown()
        {
            this->Shutdown(WorkerState::Drain);
        }

        /// <summary>
        /// Stops processing all items, including the next single item in the queue.
        ///
        /// Blocks the current thread until the queue has successfully processed the current item.
        ///
        /// Leaves items in memory, and won't drain the queue on shutdown, causing items to be lost.
        /// </summary>
        void ShutdownAndDrop()
        {
            this->Shutdown(WorkerState::Drop);
        }

        void SetIdleTimeout(const std::chrono::milliseconds idleTimeout)
        {
            if (this->HasEverBeenStarted())
            {
                throw std::logic_error("Cannot change debounce timeout while worker is running");
            }

            m_idleTimeout = idleTimeout;
        }

        void SetItemThreshold(const size_t itemThreshold)
        {
            if (this->HasEverBeenStarted())
            {
                throw std::logic_error("Cannot change debounce item threshold while worker is running");
            }

            m_itemThreshold = itemThreshold;
        }

    private:
        enum class WorkerState
        {
            /// <summary>
            /// Queue has never started
            /// </summary>
            None,

            /// <summary>
            /// Queue is currently processing items, and will keep
            /// running until otherwise signaled. This will process
            /// and post process items.
            /// </summary>
            Running,

            /// <summary>
            /// When signaled, it will process the current batch of
            /// work, but won't post process it. From this state, it
            /// can be started again, and will pick up where it left
            /// off.
            ///
            /// Intended to be used when we just want to leave
            /// items in memory, and don't mind if we loose them.
            /// </summary>
            Paused,

            /// <summary>
            /// Process all current items in the queue, even if they we're
            /// added after the current batch had started processing. This
            /// is intended to be used for a clean shutdown once all the
            /// in memory items have been successfully processed.
            /// </summary>
            Drain,

            /// <summary>
            /// Stop processing any items or batches, including any batch you
            /// are in the middle of handling, and leave everything in memory
            /// </summary>
            Drop,

            /// <summary>
            /// Queue had started, and has subsequently shutdown.
            /// </summary>
            Shutdown
        };

        /// <summary>
        /// Should we keep processing _individual_ items in the batch
        /// The idea being that if we're running, and not shutdown or
        /// dropping (E.g draining, paused, running), we should keep
        /// doing potentially long running work on individual items.
        /// </summary>
        bool ShouldKeepProcessingItems() const
        {
            WorkerState currentState = m_state;
            return ((currentState > WorkerState::None) && (currentState < WorkerState::Drop));
        }

        bool HasEverBeenStarted() const
        {
            return (m_state != WorkerState::None);
        }

        void Shutdown(const WorkerState targetState)
        {
            WorkerState previousState = m_state;
            m_state = targetState;

            TRACE_OUT(m_tracePrefix + L": Shutting down");
            if ((previousState != WorkerState::Running) && (previousState != WorkerState::Paused))
            {
                TRACE_OUT(m_tracePrefix + L": Not actually started");
                return;
            }

            if ((previousState == WorkerState::Paused) && (targetState != WorkerState::Paused) && (this->GetQueueLength() > 0))
            {
                TRACE_OUT(m_tracePrefix + L": Worker was paused, starting again to allow draining");
                this->Start();
            }

            CancelConcurrencyTimer(m_idleTimer, m_idleTimerCallback);

            if (m_workerThread.joinable())
            {
                TRACE_OUT(m_tracePrefix + L": Waiting on Worker Thread");
                m_state = targetState;
                m_hasItems.notify_one();
                m_workerThread.join();
                assert(m_state != WorkerState::Running);
            }

            TRACE_OUT(m_tracePrefix + L": Shutdown");
        }

        void TriggerWorkOrWaitForIdle()
        {
            if (m_state != WorkerState::Running)
            {
                TRACE_OUT(m_tracePrefix + L": Skipping triggering worker, since it's not started");
                return;
            }

            CancelConcurrencyTimer(m_idleTimer, m_idleTimerCallback);

            if (this->GetQueueLength() >= m_itemThreshold)
            {
                m_hasItems.notify_one();
            }
            else
            {
                auto timer = CreateConcurrencyTimer(m_idleTimeout, [this]() {
                    TRACE_OUT(m_tracePrefix + L": Debounce Timer tiggered");
                    m_hasItems.notify_one();
                });

                m_idleTimer = std::get<0>(timer);
                m_idleTimerCallback = std::get<1>(timer);

                m_idleTimer->start();
            }
        }

        void Worker()
        {
            // When we make the first iteration through the loops
            // we will want to wait if there aren't enough items in
            // the queue yet.
            bool waitForFirstWakeUp = this->GetQueueLength() <= m_itemThreshold;
            m_state = WorkerState::Running;

            {
                // Signal to people who started us that we're
                // now actually started. This allows us to be
                // sure in the starting location that we're
                // in the loop, and not going to stop any state
                // (e.g. the worker state)
                std::unique_lock<mutex> lock(m_workerLock);
                m_hasWorkerStarted.notify_one();
            }

            while (m_state < WorkerState::Shutdown)
            {
                TRACE_OUT(m_tracePrefix + L": Worker Starting Loop Iteration");
                ItemTypeVector itemsToPersist;

                {
                    std::unique_lock<std::mutex> lock(m_itemsLock);

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
                            if (m_state > WorkerState::Running)
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
                    lock_guard_mutex lock(m_itemsLock);

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

            m_state = (m_state == WorkerState::Paused) ? WorkerState::Paused : WorkerState::Shutdown;
        }

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

        // Callbacks
        std::function<ItemTypeVector(ItemTypeVector&, const std::function<bool()>&)> m_processItemsCallback;
        std::function<void(ItemTypeVector&)> m_postProcessItemsCallback;

        // Idle timeout / item limits
        std::shared_ptr<concurrency::timer<int>> m_idleTimer;
        std::shared_ptr<concurrency::call<int>> m_idleTimerCallback;
        std::chrono::milliseconds m_idleTimeout;
        size_t m_itemThreshold;

        // Items & Concurrency
        std::vector<ItemType_ptr> m_items;
        std::mutex m_itemsLock;
        std::condition_variable m_hasItems;

        // Worker state & concurrency
        std::mutex m_workerLock;
        std::condition_variable m_hasWorkerStarted;
        std::thread m_workerThread;
        std::atomic<WorkerState> m_state;

        std::wstring m_tracePrefix;
    };
} } }
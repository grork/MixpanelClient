#pragma once

#include <agents.h>
#include "Tracing.h"

namespace CodevoidN { namespace Utilities { namespace Mixpanel {
    struct PayloadContainer
    {
        PayloadContainer(long long id, Windows::Data::Json::JsonObject^ payload) :
            Id(id), Payload(payload)
        {
        }

        long long Id;
        Windows::Data::Json::JsonObject^ Payload;
    };

    bool operator==(const std::shared_ptr<PayloadContainer>& a, const std::shared_ptr<PayloadContainer>& b);

    enum class WorkerState
    {
        None,
        Drain,
        Drop,
        Shutdown
    };

    class BackgroundWorker
    {
        using PayloadContainer_ptr = std::shared_ptr<PayloadContainer>;
        using PayloadContainers = std::vector<PayloadContainer_ptr>;
        using lock_guard_mutex = std::lock_guard<std::mutex>;

    public:
        BackgroundWorker(
            std::function<PayloadContainers(PayloadContainers&, const WorkerState&)> processItemsCallback,
            std::function<void(PayloadContainers&)> postProcessItemsCallback,
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
            this->Shutdown(WorkerState::Shutdown);
            TRACE_OUT(m_tracePrefix + L": Queue Destroyed");
        }

        size_t GetQueueLength()
        {
            lock_guard_mutex lock(m_accessLock);
            return m_items.size();
        }

        void AddWork(PayloadContainer_ptr& item)
        {
            TRACE_OUT(m_tracePrefix + L": Adding Item: " + std::to_wstring(item->Id));

            {
                lock_guard_mutex lock(m_accessLock);
                m_items.emplace_back(item);
            }

            TRACE_OUT(m_tracePrefix + L": Notifying worker thread");

            CancelConcurrencyTimer(m_debounceTimer, m_debounceTimerCallback);

            if (this->GetQueueLength() > m_debounceItemThreshold)
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
            m_state = WorkerState::None;
            m_workerStarted = true;
            m_workerThread = std::thread(&BackgroundWorker::Worker, this);
        }

        void Clear()
        {
            TRACE_OUT(m_tracePrefix + L": Clearing");
            lock_guard_mutex lock(m_accessLock);
            m_items.clear();
            TRACE_OUT(m_tracePrefix + L": Cleared");
        }

        void Shutdown(const WorkerState state)
        {
            TRACE_OUT(m_tracePrefix + L": Shutting down");
            m_state = state;

            CancelConcurrencyTimer(m_debounceTimer, m_debounceTimerCallback);

            if (m_workerThread.joinable())
            {
                TRACE_OUT(m_tracePrefix + L": Waiting on Worker Thread");
                m_hasItems.notify_one();
                m_workerThread.join();
                assert(!m_workerStarted);
            }

            TRACE_OUT(m_tracePrefix + L": Shutdown");
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

        void Worker()
        {
            while (m_state < WorkerState::Drop)
            {
                TRACE_OUT(m_tracePrefix + L": Worker Starting Loop Iteration");
                PayloadContainers itemsToPersist;

                {
                    std::unique_lock<std::mutex> lock(m_accessLock);

                    // If we don't have anything in the queue, lets wait for something
                    // to be in it, or to shutdown.
                    if (m_items.size() < 1)
                    {
                        TRACE_OUT(m_tracePrefix + L": Waiting for Items to process");

                        m_hasItems.wait(lock, [this]() {
                            return ((m_items.size() > 0) || (m_state > WorkerState::None));
                        });
                    }

                    itemsToPersist.assign(begin(m_items), end(m_items));
                }

                if (itemsToPersist.size() == 0)
                {
                    if (m_state == WorkerState::Drain)
                    {
                        m_state = WorkerState::Shutdown;
                    }

                    TRACE_OUT(m_tracePrefix + L": No items, exiting loop");
                    continue;
                }

                TRACE_OUT(m_tracePrefix + L": Processing Items");
                PayloadContainers successfullyProcessed = this->m_processItemsCallback(itemsToPersist, m_state);

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

                if (m_state > WorkerState::None)
                {
                    TRACE_OUT(m_tracePrefix + L": Queue shutting down, skipping post processing");
                    continue;
                }

                TRACE_OUT(m_tracePrefix + L": Post Processing");
                m_postProcessItemsCallback(successfullyProcessed);
            }

            m_workerStarted = false;
        }

        std::shared_ptr<concurrency::timer<int>> m_debounceTimer;
        std::shared_ptr<concurrency::call<int>> m_debounceTimerCallback;
        std::chrono::milliseconds m_debounceTimeout;
        size_t m_debounceItemThreshold;
        std::function<PayloadContainers(PayloadContainers&, const WorkerState&)> m_processItemsCallback;
        std::function<void(PayloadContainers)> m_postProcessItemsCallback;
        std::wstring m_tracePrefix;
        std::vector<PayloadContainer_ptr> m_items;
        std::mutex m_accessLock;
        std::condition_variable m_hasItems;
        std::atomic<bool> m_workerStarted;
        std::thread m_workerThread;
        WorkerState m_state;
    };
} } }
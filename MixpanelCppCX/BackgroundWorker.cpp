#include "pch.h"
#include "BackgroundWorker.h"

#include "Tracing.h"

using namespace CodevoidN::Utilities::Mixpanel;
using namespace concurrency;
using namespace std;
using namespace std::chrono;
using namespace Windows::Data::Json;

using PayloadContainer_ptr = shared_ptr<PayloadContainer>;
using PayloadContainers = vector<PayloadContainer_ptr>;

bool FindPayloadWithId(const PayloadContainer_ptr& other, const long long id)
{
    return other->Id == id;
}

PayloadContainer::PayloadContainer(long long id, JsonObject^ payload) :
    Id(id), Payload(payload)
{
}

// concurrency::timer is weird, and requires you to set things up in weird ways
// (hence the pointers). Also, it can't be "Restarted" if it's a single timer
// so we need to recreate it every time. This function just wraps things up neatly.
tuple<shared_ptr<timer<int>>, shared_ptr<call<int>>> CreateConcurrencyTimer(milliseconds timeout, function<void()> callback)
{
    auto callWrapper = make_shared<call<int>>([callback](int) {
        callback();
    });

    auto timerInstance = make_shared<concurrency::timer<int>>((int)timeout.count(), 0, callWrapper.get(), false);

    return make_tuple(timerInstance, callWrapper);
}

// Per CreateConcurrencyTimer, this helps clean the timers up.
void CancelConcurrencyTimer(shared_ptr<timer<int>>& timer, shared_ptr<call<int>>& target)
{
    if (timer != nullptr && target != nullptr)
    {
        timer->stop();
        timer->unlink_target(target.get());
    }

    target = nullptr;
    timer = nullptr;
}

BackgroundWorker::BackgroundWorker(
    function<PayloadContainers(PayloadContainers&, const WorkerState&)> processItemsCallback,
    function<void(PayloadContainers&)> postProcessItemsCallback,
    const wstring& tracePrefix,
    milliseconds debounceTimeout,
    size_t debounceItemThreshold
) :
    m_processItemsCallback(processItemsCallback),
    m_postProcessItemsCallback(postProcessItemsCallback),
    m_tracePrefix(tracePrefix),
    m_workerStarted(false),
    m_state(WorkerState::None),
    m_debounceTimeout(debounceTimeout),
    m_debounceItemThreshold(debounceItemThreshold)
{
}

void BackgroundWorker::SetDebounceTimeout(milliseconds debounceTimeout)
{
    if (m_workerStarted)
    {
        throw std::logic_error("Cannot change debounce timeout while worker is running");
    }

    m_debounceTimeout = debounceTimeout;
}

void BackgroundWorker::SetDebounceItemThreshold(size_t debounceItemThreshold)
{
    if (m_workerStarted)
    {
        throw std::logic_error("Cannot change debounce item threshold while worker is running");
    }

    m_debounceItemThreshold = debounceItemThreshold;
}

BackgroundWorker::~BackgroundWorker()
{
    TRACE_OUT(m_tracePrefix + L": Queue being destroyed");
    this->Shutdown(WorkerState::Shutdown);
    TRACE_OUT(m_tracePrefix + L": Queue Destroyed");
}

size_t BackgroundWorker::GetQueueLength()
{
    lock_guard<mutex> lock(m_accessLock);
    return m_items.size();
}

void BackgroundWorker::AddWork(PayloadContainer_ptr& item)
{
    TRACE_OUT(m_tracePrefix + L": Adding Item: " + to_wstring(item->Id));

    {
        lock_guard<mutex> lock(m_accessLock);
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

        m_debounceTimer = get<0>(timer);
        m_debounceTimerCallback = get<1>(timer);

        m_debounceTimer->start();
    }
}

void BackgroundWorker::Clear()
{
    TRACE_OUT(m_tracePrefix + L": Clearing");
    lock_guard<mutex> lock(m_accessLock);
    m_items.clear();
    TRACE_OUT(m_tracePrefix + L": Cleared");
}

void BackgroundWorker::Shutdown(const WorkerState state)
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

void BackgroundWorker::Start()
{
    if (m_processItemsCallback == nullptr)
    {
        throw invalid_argument("Must provide a ProcessItems function start worker");
    }

    if (m_postProcessItemsCallback == nullptr)
    {
        throw invalid_argument("Must provide a PostProcessItems function start worker");
    }

    if (m_workerStarted)
    {
        TRACE_OUT(m_tracePrefix + L"Write To Storage worker already running");
        return;
    }

    TRACE_OUT(m_tracePrefix + L": Starting Write To Storage worker");
    m_state = WorkerState::None;
    m_workerStarted = true;
    m_workerThread = thread(&BackgroundWorker::Worker, this);
}

void BackgroundWorker::Worker()
{
    while (m_state < WorkerState::Drop)
    {
        TRACE_OUT(m_tracePrefix + L": Worker Starting Loop Iteration");
        PayloadContainers itemsToPersist;

        {
            unique_lock<mutex> lock(m_accessLock);

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
            lock_guard<mutex> lock(m_accessLock);

            TRACE_OUT(m_tracePrefix + L": Clearing Queue of processed items");

            // Remove the items from the list that had been successfully processed
            // This requires us to loop through the list multiple times (boo), since
            // we're changing the list, and they may be non-contiguous. Hopefully,
            // however, the items are near the front of the list since thats what we
            // were processing.
            for (auto&& processedItem : successfullyProcessed)
            {
                auto removeAt = find_if(begin(m_items),
                    end(m_items),
                    bind(&FindPayloadWithId, placeholders::_1, processedItem->Id));

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
#include "pch.h"
#include "BackgroundWorker.h"
#include "Tracing.h"

using namespace CodevoidN::Utilities::Mixpanel;
using namespace std;
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

BackgroundWorker::BackgroundWorker(
    function<PayloadContainers(PayloadContainers&, const ShutdownState&)> processItemsCallback,
    function<void(PayloadContainers&)> postProcessItemsCallback,
    wstring& tracePrefix
) :
    m_processItemsCallback(processItemsCallback),
    m_postProcessItemsCallback(postProcessItemsCallback),
    m_tracePrefix(tracePrefix),
    m_workerStarted(false),
    m_shutdownState(ShutdownState::None)
{

}

BackgroundWorker::~BackgroundWorker()
{
    TRACE_OUT(m_tracePrefix + L": Queue being destroyed");
    this->Shutdown(ShutdownState::Shutdown);
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
    m_hasItems.notify_one();
}

void BackgroundWorker::Clear()
{
    TRACE_OUT(m_tracePrefix + L": Clearing");
    lock_guard<mutex> lock(m_accessLock);
    m_items.clear();
    TRACE_OUT(m_tracePrefix + L": Cleared");
}

void BackgroundWorker::Shutdown(const ShutdownState state)
{
    TRACE_OUT(m_tracePrefix + L": Shutting down");
    m_shutdownState = state;

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

    TRACE_OUT(m_tracePrefix + L"Starting Write To Storage worker");
    m_workerStarted = true;
    m_workerThread = thread(&BackgroundWorker::Worker, this);
}

void BackgroundWorker::Worker()
{
    while (m_shutdownState < ShutdownState::Drop)
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
                    return ((m_items.size() > 0) || (m_shutdownState > ShutdownState::None));
                });
            }

            itemsToPersist.assign(begin(m_items), end(m_items));
        }

        if (itemsToPersist.size() == 0)
        {
            if (m_shutdownState == ShutdownState::Drain)
            {
                m_shutdownState = ShutdownState::Shutdown;
            }

            TRACE_OUT(m_tracePrefix + L": No items, exiting loop");
            continue;
        }

        TRACE_OUT(m_tracePrefix + L": Processing Items");
        PayloadContainers& successfullyProcessed = this->m_processItemsCallback(itemsToPersist, m_shutdownState);

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

        if (m_shutdownState > ShutdownState::None)
        {
            TRACE_OUT(m_tracePrefix + L": Queue shutting down, skipping post processing");
            continue;
        }

        TRACE_OUT(m_tracePrefix + L": Post Processing");
        m_postProcessItemsCallback(successfullyProcessed);
    }

    m_workerStarted = false;
}
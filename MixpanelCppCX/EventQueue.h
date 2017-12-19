#pragma once

#include "BackgroundWorker.h"

namespace CodevoidN { namespace  Tests { namespace Mixpanel {
    class EventQueueTests;
} } }

namespace CodevoidN { namespace Utilities { namespace Mixpanel {
    enum class EventPriority
    {
        Normal,
        Low,
    };

    struct PayloadContainer
    {
        PayloadContainer(const long long id,
            Windows::Data::Json::JsonObject^ payload,
            const EventPriority priority) :
            Id(id), Payload(payload), Priority(priority)
        {
        }

        long long Id;
        Windows::Data::Json::JsonObject^ Payload;
        EventPriority Priority;
    };

    /// <summary>
    /// MixpanelClient offers a API for interacting with Mixpanel for UWP apps running on Windows 10+
    /// </summary>
    class EventQueue
    {
        friend class CodevoidN::Tests::Mixpanel::EventQueueTests;

    public:
        EventQueue(
            Windows::Storage::StorageFolder^ localStorage
        );
        ~EventQueue();

        /// <summary>	
        /// Adds <paramref name="data" /> to the queue, and returns of the ID added to that data object.
        ///
        /// Items placed in the queue are processed after being first written to storage.
        /// </summary>
        long long QueueEventForUpload(Windows::Data::Json::JsonObject^ data, const EventPriority& priority = EventPriority::Normal);

        /// <summary>
        /// Waits for the queued items to be written to disk before
        /// returning to the caller.
        /// </summary>
        concurrency::task<void> PersistAllQueuedItemsToStorageAndShutdown();

        /// <summary>
        /// Restores the queue state from any saved state on disk.
        /// Completes when it's finished loading from disk, and the
        /// data is now available in the queue.
        /// </summary>
        concurrency::task<void> RestorePendingUploadQueueFromStorage();

        /// <summary>
        /// Clears any items in the queue, and from storage.
        /// </summary>
        concurrency::task<void> Clear();

        /// <summary>
        /// The number of items currently waiting to be persisted to storage
        /// </summary>
        std::size_t GetWaitingToWriteToStorageLength();

        /// <summary>
        /// Number of items in the upload queue.
        /// </summary>
        std::size_t GetWaitingForUploadLength();

        /// <summary>
        /// Start logging any items queue to disk.
        /// </summary>
        void EnableQueuingToStorage();

    private:
        enum class QueueState
        {
            None, // Queue has neve been startred
            Running, // Queue is running
            Drain, // Queue is draining the current items
            Drop, // Drop all the items, with no care to where to put them
            Stopped // We've successfully stopped
        };

        std::atomic<long long> m_baseId;
        QueueState m_state;

        Windows::Storage::StorageFolder^ m_localStorage;
        CodevoidN::Utilities::Mixpanel::BackgroundWorker<PayloadContainer> m_writeToStorageWorker;

        std::vector<std::shared_ptr<PayloadContainer>> m_waitingForUpload;
        std::mutex m_waitingForUploadQueueLock;

        /// <summary>
        /// When we're writing the files to disk, we use a 'base' ID created
        /// at startup to help avoid conflicts with time. This method isolates
        /// the atomic incrementing of the counter so multiple threads/callers
        /// can avoid other clashes.
        /// </summary>
        long long GetNextId();

        concurrency::task<void> WriteItemToStorage(const std::shared_ptr<PayloadContainer> item);
        std::vector<std::shared_ptr<PayloadContainer>> WriteItemsToStorage(const std::vector<std::shared_ptr<PayloadContainer>>& items, const std::function<bool()>& shouldKeepProcessing);
        void AddItemsToUploadQueue(const std::vector<std::shared_ptr<PayloadContainer>>& itemsToUpload);
        concurrency::task<void> ClearStorage();

        /// <summary>
        /// Configures the idle limits for the write to storage behaviour.
        /// This overrides the defaults, soley for testing purposes.
        /// </summary>
        void SetWriteToStorageIdleLimits(const std::chrono::milliseconds& idleTimeout, const size_t& idleItemThreshold);
    };
} } }
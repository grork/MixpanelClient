#pragma once

#include <mutex>
#include <atomic>

namespace CodevoidN { namespace  Tests { namespace Mixpanel {
    class EventQueueTests;
} } }

namespace CodevoidN { namespace Utilities { namespace Mixpanel {
    /// <summary>
    /// MixpanelClient offers a API for interacting with Mixpanel for UWP apps running on Windows 10+
    /// </summary>
    class EventQueue
    {
        friend class EventQueueTests;

    public:
        struct PayloadContainer
        {
            PayloadContainer(long long id, Windows::Data::Json::JsonObject^ payload);
            long long Id;
            Windows::Data::Json::JsonObject^ Payload;
        };

        EventQueue(Windows::Storage::StorageFolder^ localStorage);
        ~EventQueue();

		/// <summary>	
		/// Adds <paramref name="data" /> to the queue, and returns of the ID added to that data object.
		///
		/// Items placed in the queue are processed after being first written to storage.
		/// </summary>
        long long QueueEventForUpload(Windows::Data::Json::JsonObject^ data);

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
        enum class ShutdownState
        {
            None,
            Drain,
            Drop,
            Shutdown
        };

        bool m_queueToStorage = true;
        std::atomic<long long> m_baseId;
        std::optional<ShutdownState> m_shutdownState;

        Windows::Storage::StorageFolder^ m_localStorage;

        // Writing to storage work
        std::vector<std::shared_ptr<PayloadContainer>> m_writeToStorageQueue;
        std::condition_variable m_writeToStorageQueueReady;
        std::mutex m_writeToStorageQueueLock;
        std::atomic<bool> m_writeToStorageWorkerStarted;
        std::thread m_writeToStorageWorker;
        void PersistToStorageWorker();

        std::vector<std::shared_ptr<PayloadContainer>> m_waitingForUpload;
        std::mutex m_waitingForUploadQueueLock;
        
        /// <summary>
        /// When we're writing the files to disk, we use a 'base' ID created
        /// at startup to help avoid conflicts with time. This method isolates
        /// the atomic incrementing of the counter so multiple threads/callers
        /// can avoid other clashes.
        /// </summary>
        long long GetNextId();

        static bool FindPayloadWithId(const std::shared_ptr<PayloadContainer>& other, const long long id);
        concurrency::task<void> WriteItemToStorage(const std::shared_ptr<PayloadContainer> item);
        concurrency::task<void> ClearStorage();
    };
} } }
    
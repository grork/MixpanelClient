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

        enum class AddToUploadQueue
        {
            No,
            Yes
        };

        EventQueue(Windows::Storage::StorageFolder^ localStorage);

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
        concurrency::task<void> PersistAllQueuedItemsToStorage();

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
        /// The number of items currently in the queue
        /// </summary>
        std::size_t GetWaitingToWriteToStorageLength();

        std::size_t GetWaitingForUploadLength();

        /// <summary>
        /// Stop logging any items queue to disk. Note, that anything
        /// queued before enable is called again will not be saved to
        /// disk.
        /// </summary>
        void DisableQueuingToStorage();
        void EnableQueuingToStorage();

    private:
        bool m_queueToStorage = true;
        std::atomic<long long> m_baseId;
        Windows::Storage::StorageFolder^ m_localStorage;

        std::vector<std::shared_ptr<PayloadContainer>> m_waitingToWriteToStorage;
        std::mutex m_writeToStorageQueueLock;

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
        concurrency::task<void> WriteCurrentQueueStateToStorage(AddToUploadQueue addToUpload = AddToUploadQueue::Yes);
    };
} } }
    
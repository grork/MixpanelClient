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

    private:
        struct PayloadContainer
        {
            long long Id;
            Windows::Data::Json::JsonObject^ Payload;
            bool Persisted;
        };

		static bool find_payload(const PayloadContainer& other, const long long id);

    public:
        EventQueue(Windows::Storage::StorageFolder^ localStorage);

		/// <summary>	
		/// Adds <paramref name="data" /> to the queue, and returns of the ID added to that data object.
		///
		/// Items placed in the queue are processed after being first written to storage.
		/// </summary>
        long long QueueEventForUpload(Windows::Data::Json::JsonObject^ data);

		/// <summary>
		/// Removes the event represented by the supplied ID
		/// from the queue & from the storage, if it is still present.
		/// </summary>
        void RemoveEventFromUploadQueue(long long id);

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
        concurrency::task<void> RestorePendingUploadQueue();

        /// <summary>
        /// Clears any items in the queue, and from storage.
        /// </summary>
        concurrency::task<void> Clear();

        /// <summary>
        /// The number of items currently in the queue
        /// </summary>
        std::size_t GetQueueLength();

        /// <summary>
        /// Stop logging any items queue to disk. Note, that anything
        /// queued before enable is called again will not be saved to
        /// disk.
        /// </summary>
        void DisableQueuingToStorage();
        void EnableQueuingToStorage();

    private:
        bool m_queueToDisk = true;
        std::atomic<long long> m_baseId;
        std::vector<PayloadContainer> m_queue;
        Windows::Storage::StorageFolder^ m_localStorage;

        /// <summary>
        /// When we're writing the files to disk, we use a 'base' ID created
        /// at startup to help avoid conflicts with time. This method isolates
        /// the atomic incrementing of the counter so multiple threads/callers
        /// can avoid other clashes.
        /// </summary>
        long long GetNextId();
        concurrency::task<void> WriteItemToStorage(PayloadContainer& item);
        concurrency::task<void> RemoveItemFromStorage(long long id);
        concurrency::task<void> ClearStorage();

		std::mutex m_queueAccessLock;
    };
} } }
    
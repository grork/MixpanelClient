#pragma once

#include <mutex>

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

        struct find_payload : std::unary_function<PayloadContainer, bool>
        {
            long long candidate_id;
            find_payload(long long id) : candidate_id(id) { };
            bool operator() (const PayloadContainer& other) const
            {
                return other.Id == candidate_id;
            }
        };

    public:
        EventQueue(Windows::Storage::StorageFolder^ localStorage) : m_localStorage(localStorage)
        {
            if (localStorage == nullptr)
            {
                throw std::invalid_argument("Must provide local storage folder");
            }
        }

		/// <summary>
		/// Restores the queue state from any saved state on disk.
		/// Completes when it's finished loading from disk, and the
		/// data is now available in the queue.
		/// </summary>
		concurrency::task<void> RestorePendingUploadQueue();

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
        /// Clears any items in the queue, and from storage.
        /// </summary>
        void Clear();

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
        std::vector<PayloadContainer> m_queue;
        Windows::Storage::StorageFolder^ m_localStorage;

        concurrency::task<void> WriteItemToStorage(PayloadContainer& item);
        concurrency::task<void> RemoveItemFromStorage(long long id);
        concurrency::task<void> ClearStorage();

		std::mutex m_queueAccessLock;
    };
} } }
    
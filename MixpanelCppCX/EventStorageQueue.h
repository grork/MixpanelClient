#pragma once

#include "BackgroundWorker.h"

namespace CodevoidN { namespace  Tests { namespace Mixpanel {
    class EventStorageQueueTests;
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
            Windows::Data::Json::IJsonValue^ payload,
            const EventPriority priority) :
            Id(id), Payload(payload), Priority(priority)
        {
        }

        long long Id;
        Windows::Data::Json::IJsonValue^ Payload;
        EventPriority Priority;
    };

    /// <summary>
    /// MixpanelClient offers a API for interacting with Mixpanel for UWP apps running on Windows 10+
    /// </summary>
    class EventStorageQueue
    {
        friend class CodevoidN::Tests::Mixpanel::EventStorageQueueTests;

    public:
        EventStorageQueue(
            Windows::Storage::StorageFolder^ localStorage,
            std::function<void(const std::vector<std::shared_ptr<PayloadContainer>>&)> writtenToStorageCallback
        );
        ~EventStorageQueue();

        /// <summary>	
        /// Adds <paramref name="data" /> to the queue, and returns of the ID added to that data object.
        ///
        /// Items placed in the queue are processed after being first written to storage.
        /// </summary>
        long long QueueEventToStorage(Windows::Data::Json::IJsonValue^ data, const EventPriority& priority = EventPriority::Normal);

        /// <summary>
        /// Waits for the queued items to be written to disk before
        /// returning to the caller.
        /// </summary>
        concurrency::task<void> PersistAllQueuedItemsToStorageAndShutdown();

        /// <summary>
        /// Loads any persisted items from storage.
        /// Completes when it's finished loading from disk, and returns
        /// those items to the caller
        /// </summary>
        static concurrency::task<std::vector<std::shared_ptr<PayloadContainer>>> LoadItemsFromStorage(Windows::Storage::StorageFolder^ folder);

        /// <summary>
        /// Clears any items in the queue, and from storage.
        /// </summary>
        concurrency::task<void> Clear();

        /// <summary>
        /// The number of items currently waiting to be persisted to storage
        /// </summary>
        std::size_t GetWaitingToWriteToStorageLength();

        /// <summary>
        /// Start logging any items queue to disk.
        /// </summary>
        void EnableQueuingToStorage();

        /// <summary>
        /// Removes the supplied event from storage if it is present
        /// If it's still in the queue, that is left in place.
        /// </summary>
        concurrency::task<void> RemoveEventFromStorage(PayloadContainer& container);

        /// <summary>
        /// Configures the idle limits for the write to storage behaviour.
        /// This overrides the defaults, soley for testing purposes.
        /// </summary>
        void SetWriteToStorageIdleLimits(const std::chrono::milliseconds& idleTimeout, const size_t& idleItemThreshold);

        /// <summary>
        /// Disables the act of writing the payloads to disk to help with testing.
        /// Intended for 2nd level test cases which are validating their composed behaviour
        /// but don't really need anything written to disk.
        /// </summary>
        void DontWriteToStorageForTestPurposeses();

        /// <summary>
        /// Forced the act of writing the payloads to disk to help with testing.
        /// Intended for 2nd level test cases which are validating their composed behaviour
        /// really does want things written to disk.
        /// </summary>
        void NoReallyWriteToStorageDuringTesting();

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
        std::atomic<QueueState> m_state;

        Windows::Storage::StorageFolder^ m_localStorage;
        CodevoidN::Utilities::BackgroundWorker<PayloadContainer> m_writeToStorageWorker;
        bool m_dontWriteToStorageForTestPurposes;

        std::function<void(const std::vector<std::shared_ptr<PayloadContainer>>&)> m_writtenToStorageCallback;

        /// <summary>
        /// When we're writing the files to disk, we use a 'base' ID created
        /// at startup to help avoid conflicts with time. This method isolates
        /// the atomic incrementing of the counter so multiple threads/callers
        /// can avoid other clashes.
        /// </summary>
        long long GetNextId();

        concurrency::task<void> WriteItemToStorage(const std::shared_ptr<PayloadContainer> item);
        std::vector<std::shared_ptr<PayloadContainer>> WriteItemsToStorage(const std::vector<std::shared_ptr<PayloadContainer>>& items, const std::function<bool()>& shouldKeepProcessing);
        void HandleProcessedItems(const std::vector<std::shared_ptr<PayloadContainer>>& itemsToUpload);
        concurrency::task<void> ClearStorage();
    };
} } }
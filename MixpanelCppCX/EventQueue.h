#pragma once

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

        concurrency::task<void> RestoreQueue();
        long long QueueEvent(Windows::Data::Json::JsonObject^ data);
        void RemoveEvent(long long id);
        void Clear();
        std::size_t GetQueueLength();

        void DisableQueuingToStorage();
        void EnableQueuingToStorage();

    private:
        bool m_queueToDisk = true;
        std::vector<PayloadContainer> m_queue;
        Windows::Storage::StorageFolder^ m_localStorage;

        concurrency::task<void> QueueToStorage(PayloadContainer& item);
        concurrency::task<void> RemoveFromStorage(long long id);
        concurrency::task<void> ClearStorage();
    };
} } }
    
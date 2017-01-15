#pragma once

namespace CodevoidN { namespace Utilities { namespace Mixpanel {
    /// <summary>
    /// MixpanelClient offers a API for interacting with Mixpanel for UWP apps running on Windows 10+
    /// </summary>
    class EventQueue
    {
    private:
        struct PayloadContainer
        {
            long long Id;
            Windows::Data::Json::JsonObject^ Payload;
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
        long long QueueEvent(Windows::Data::Json::JsonObject^ data);
        void RemoveEvent(long long id);
        std::size_t GetQueueLength();

    private:
        std::vector<PayloadContainer> m_queue;
    };
} } }
    
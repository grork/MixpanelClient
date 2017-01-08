#pragma once

namespace CodevoidN { namespace Utilities { namespace Mixpanel {
    /// <summary>
    /// MixpanelClient offers a API for interacting with Mixpanel for UWP apps running on Windows 10+
    /// </summary>
    class EventQueue
    {
    public:
        void QueueEvent(Windows::Data::Json::JsonObject^ data);
        std::size_t GetQueueLength();

    private:
        std::vector<Windows::Data::Json::JsonObject^> m_queue;
    };
} } }
    
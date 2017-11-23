#pragma once
namespace CodevoidN { namespace Utilities { namespace Mixpanel {
    struct PayloadContainer
    {
        PayloadContainer(long long id, Windows::Data::Json::JsonObject^ payload);
        long long Id;
        Windows::Data::Json::JsonObject^ Payload;
    };

    enum class ShutdownState
    {
        None,
        Drain,
        Drop,
        Shutdown
    };

    class BackgroundWorker
    {
    public:
        BackgroundWorker(
            std::function<std::vector<std::shared_ptr<PayloadContainer>>(std::vector<std::shared_ptr<PayloadContainer>>&, const ShutdownState&)> processItemsCallback,
            std::function<void(std::vector<std::shared_ptr<PayloadContainer>>&)> postProcessItemsCallback,
            std::wstring& tracePrefix);
        ~BackgroundWorker();

        size_t GetQueueLength();
        void AddWork(std::shared_ptr<PayloadContainer>& item);
        void Start();
        void Clear();
        void Shutdown(const ShutdownState state);

    private:
        std::function<std::vector<std::shared_ptr<PayloadContainer>>(std::vector<std::shared_ptr<PayloadContainer>>&, const ShutdownState&)> m_processItemsCallback;
        std::function<void(std::vector<std::shared_ptr<PayloadContainer>>&)> m_postProcessItemsCallback;
        std::wstring m_tracePrefix;
        std::vector<std::shared_ptr<PayloadContainer>> m_items;
        std::mutex m_accessLock;
        std::condition_variable m_hasItems;
        std::atomic<bool> m_workerStarted;
        std::thread m_workerThread;
        ShutdownState m_shutdownState;
        void Worker();
    };
} } }
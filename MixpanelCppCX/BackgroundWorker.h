#pragma once
#include <agents.h>

namespace CodevoidN { namespace Utilities { namespace Mixpanel {
    struct PayloadContainer
    {
        PayloadContainer(long long id, Windows::Data::Json::JsonObject^ payload);
        long long Id;
        Windows::Data::Json::JsonObject^ Payload;
    };

    enum class WorkerState
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
            std::function<std::vector<std::shared_ptr<PayloadContainer>>(std::vector<std::shared_ptr<PayloadContainer>>&, const WorkerState&)> processItemsCallback,
            std::function<void(std::vector<std::shared_ptr<PayloadContainer>>&)> postProcessItemsCallback,
            const std::wstring& tracePrefix,
            const std::chrono::milliseconds debounceTimeout = std::chrono::milliseconds(500),
            size_t debounceItemThreshold = 10);
        ~BackgroundWorker();

        size_t GetQueueLength();
        void AddWork(std::shared_ptr<PayloadContainer>& item);
        void Start();
        void Clear();
        void Shutdown(const WorkerState state);
        void SetDebounceTimeout(std::chrono::milliseconds debounceTimeout);
        void SetDebounceItemThreshold(size_t debounceItemThreshold);

    private:
        std::shared_ptr<concurrency::timer<int>> m_debounceTimer;
        std::shared_ptr<concurrency::call<int>> m_debounceTimerCallback;
        std::chrono::milliseconds m_debounceTimeout;
        size_t m_debounceItemThreshold;
        std::function<std::vector<std::shared_ptr<PayloadContainer>>(std::vector<std::shared_ptr<PayloadContainer>>&, const WorkerState&)> m_processItemsCallback;
        std::function<void(std::vector<std::shared_ptr<PayloadContainer>>&)> m_postProcessItemsCallback;
        std::wstring m_tracePrefix;
        std::vector<std::shared_ptr<PayloadContainer>> m_items;
        std::mutex m_accessLock;
        std::condition_variable m_hasItems;
        std::atomic<bool> m_workerStarted;
        std::thread m_workerThread;
        WorkerState m_state;
        void Worker();
    };
} } }
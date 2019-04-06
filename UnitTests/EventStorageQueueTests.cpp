#include "pch.h"
#include <memory>

#include "CppUnitTest.h"
#include "AsyncHelper.h"
#include "EventStorageQueue.h"

using namespace Platform;
using namespace std;
using namespace Codevoid::Tests::Utilities;
using namespace Codevoid::Utilities::Mixpanel;
using namespace concurrency;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Windows::Data::Json;
using namespace Windows::Foundation::Collections;
using namespace Windows::Storage;

namespace Codevoid::Tests::Mixpanel
{
    static task<StorageFolder^> GetAndClearTestFolder()
    {
        auto storageFolder = co_await ApplicationData::Current->LocalFolder->CreateFolderAsync("EventStorageQueueTests",
            CreationCollisionOption::OpenIfExists);

        auto files = co_await storageFolder->GetFilesAsync();

        if (files->Size > 0)
        {
            for (auto&& fileToDelete : files)
            {
                co_await fileToDelete->DeleteAsync(StorageDeleteOption::PermanentDelete);
            }
        }

        return storageFolder;
    }

    static JsonObject^ GenerateSamplePayload()
    {
        JsonObject^ payload = ref new JsonObject();

        auto title = JsonValue::CreateStringValue(L"SampleTitle");
        payload->Insert(L"title", title);

        return payload;
    }

    TEST_CLASS(EventStorageQueueTests)
    {
    private:
        shared_ptr<EventStorageQueue> m_queue;
        StorageFolder^ m_queueFolder;
        vector<shared_ptr<PayloadContainer>> m_writtenItems;
        function<void(const vector<shared_ptr<PayloadContainer>>&)> m_processWrittenItemsCallback;
        mutex m_writtenItemsLock;

        void ProcessAllWrittenItems(const vector<shared_ptr<PayloadContainer>>& items)
        {
            lock_guard<mutex> lock(m_writtenItemsLock);
            m_writtenItems.insert(end(m_writtenItems), begin(items), end(items));
        }

        size_t GetWrittenItemsSize()
        {
            lock_guard<mutex> lock(m_writtenItemsLock);
            return m_writtenItems.size();
        }

    public:
        TEST_METHOD_INITIALIZE(InitializeClass)
        {
            {
                lock_guard<mutex> lock(m_writtenItemsLock);
                m_writtenItems = vector<shared_ptr<PayloadContainer>>();
            }

            m_processWrittenItemsCallback = bind(&EventStorageQueueTests::ProcessAllWrittenItems, this, placeholders::_1);
            m_queueFolder = AsyncHelper::RunSynced(GetAndClearTestFolder());
            m_queue = make_shared<EventStorageQueue>(m_queueFolder, m_processWrittenItemsCallback);
        }

        TEST_METHOD(ConstructionThrowsIfNoFolderProvided)
        {
            bool exceptionSeen = false;
            try
            {
                auto result = make_unique<EventStorageQueue>(nullptr, nullptr);
            }
            catch (const invalid_argument&)
            {
                exceptionSeen = true;
            }

            Assert::IsTrue(exceptionSeen, L"Expected to get exception on construction");
        }

        TEST_METHOD(CanQueueEvent)
        {
            auto result = m_queue->QueueEventToStorage(GenerateSamplePayload());
            Assert::AreNotEqual(0, (int)result, L"Didn't get a token back from queueing the event");
            Assert::AreEqual(1, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Incorrect number of items");
        }

        TEST_METHOD(CanClearQueue)
        {
            m_queue->QueueEventToStorage(GenerateSamplePayload());
            m_queue->QueueEventToStorage(GenerateSamplePayload());
            Assert::AreEqual(2, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Incorrect number of items");

            AsyncHelper::RunSynced(m_queue->Clear());
            Assert::AreEqual(0, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Expected queue to be clear");
        }

        TEST_METHOD(ItemsAreQueuedToDisk)
        {
            m_queue->EnableQueuingToStorage();
            auto result = m_queue->QueueEventToStorage(GenerateSamplePayload());
            Assert::AreNotEqual(0, (int)result, L"Didn't get a token back from queueing the event");
            Assert::AreEqual(1, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Incorrect number of items");

            AsyncHelper::RunSynced(m_queue->PersistAllQueuedItemsToStorageAndShutdown());

            Assert::AreEqual(0, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Shouldn't find items waiting to be written to disk");
            Assert::AreEqual(1, (int)AsyncHelper::RunSynced(this->GetCurrentFileCountInQueueFolder()), L"Incorrect file count found");
        }

        TEST_METHOD(ItemsQueuedBeforeStartingAreSuccessfullyQueued)
        {
            auto result = m_queue->QueueEventToStorage(GenerateSamplePayload());
            Assert::AreNotEqual(0, (int)result, L"Didn't get a token back from queueing the event");
            Assert::AreEqual(1, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Incorrect number of items");

            m_queue->EnableQueuingToStorage();

            AsyncHelper::RunSynced(m_queue->PersistAllQueuedItemsToStorageAndShutdown());

            Assert::AreEqual(0, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Shouldn't find items waiting to be written to disk");
            Assert::AreEqual(1, (int)AsyncHelper::RunSynced(this->GetCurrentFileCountInQueueFolder()), L"Incorrect file count found");
        }

        TEST_METHOD(ItemsThatFailToWriteToDiskAreStillPassedToNextStageHandler)
        {
            auto payloadId = m_queue->QueueEventToStorage(GenerateSamplePayload());

            // We're going to cheat here, since we'd like at least one of the operations
            // to persist the file to disk to fail, causing the queue to get all kinds
            // of confused. What we're going to do is write a file into the location
            // based on ID so that the write fails.
            AsyncHelper::RunSynced(this->WriteEmptyPayload(payloadId));

            Assert::AreNotEqual(0, (int)payloadId, L"Didn't get a id back from queueing the event");
            Assert::AreEqual(1, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Incorrect number of items");

            m_queue->EnableQueuingToStorage();

            auto deleteAfter1Second = this->DelayAndDeleteItemWithId(payloadId);

            AsyncHelper::RunSynced(m_queue->PersistAllQueuedItemsToStorageAndShutdown());
            auto fileWasDeleted = AsyncHelper::RunSynced(deleteAfter1Second);
            Assert::IsTrue(fileWasDeleted);

            Assert::AreEqual(0, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Shouldn't find items waiting to be written to disk");
            Assert::AreEqual(0, (int)AsyncHelper::RunSynced(this->GetCurrentFileCountInQueueFolder()), L"Incorrect file count found");
            Assert::AreEqual(1, (int)m_writtenItems.size(), L"Items weren't passed to the next stage");
        }

        task<bool> DelayAndDeleteItemWithId(long long id)
        {
            std::this_thread::sleep_for(1000ms);

            auto file = co_await m_queueFolder->TryGetItemAsync(GetFileNameForId(id));
            if (file == nullptr) {
                return false;
            }

            co_await file->DeleteAsync();
            return true;
        }

        TEST_METHOD(QueuingItemsAfterShuttingDownDontGetQueued)
        {
            m_queue->EnableQueuingToStorage();
            auto result = m_queue->QueueEventToStorage(GenerateSamplePayload());
            Assert::AreNotEqual(0, (int)result, L"Didn't get a token back from queueing the event");
            Assert::AreEqual(1, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Incorrect number of items");

            AsyncHelper::RunSynced(m_queue->PersistAllQueuedItemsToStorageAndShutdown());

            Assert::AreEqual(0, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Shouldn't find items waiting to be written to disk");
            Assert::AreEqual(1, (int)AsyncHelper::RunSynced(this->GetCurrentFileCountInQueueFolder()), L"Incorrect file count found");

            result = m_queue->QueueEventToStorage(GenerateSamplePayload());
            Assert::AreEqual(0, (int)result, L"Got a result from queuing, when it was shutdown and shouldn't have");
            Assert::AreEqual(0, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Shouldn't find items waiting to be written to disk");
        }

        TEST_METHOD(QueueCanBeRestartedAfterShuttingDown)
        {
            // Queue an event, then shutdown the queue.
            m_queue->EnableQueuingToStorage();
            auto result = m_queue->QueueEventToStorage(GenerateSamplePayload());
            Assert::AreNotEqual(0, (int)result, L"Didn't get a token back from queueing the event");
            Assert::AreEqual(1, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Incorrect number of items");

            AsyncHelper::RunSynced(m_queue->PersistAllQueuedItemsToStorageAndShutdown());

            // Check the queue drained properly
            Assert::AreEqual(0, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Shouldn't find items waiting to be written to disk");
            Assert::AreEqual(1, (int)AsyncHelper::RunSynced(this->GetCurrentFileCountInQueueFolder()), L"Incorrect file count found");

            // Restart the queue, and queue an event
            m_queue->EnableQueuingToStorage();

            result = m_queue->QueueEventToStorage(GenerateSamplePayload());

            Assert::AreNotEqual(0, (int)result, L"Didn't get a token back from queueing the event");
            Assert::AreEqual(1, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Incorrect number of items");

            // Drain the queue and make sure it gets to disk.
            AsyncHelper::RunSynced(m_queue->PersistAllQueuedItemsToStorageAndShutdown());

            Assert::AreEqual(0, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Shouldn't find items waiting to be written to disk");
            // 2 'cause second write in this test
            Assert::AreEqual(2, (int)AsyncHelper::RunSynced(this->GetCurrentFileCountInQueueFolder()), L"Incorrect file count found");
        }

        TEST_METHOD(ItemsAreNotWrittenToDiskWhenAskedToSkipForTesting)
        {
            m_queue->DontWriteToStorageFolder();
            m_queue->EnableQueuingToStorage();
            auto result = m_queue->QueueEventToStorage(GenerateSamplePayload());
            Assert::AreNotEqual(0, (int)result, L"Didn't get a token back from queueing the event");
            Assert::AreEqual(1, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Incorrect number of items");

            AsyncHelper::RunSynced(m_queue->PersistAllQueuedItemsToStorageAndShutdown());

            Assert::AreEqual(0, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Shouldn't find items waiting to be written to disk");
            Assert::AreEqual(0, (int)AsyncHelper::RunSynced(this->GetCurrentFileCountInQueueFolder()), L"Incorrect file count found");
        }

        TEST_METHOD(ItemsAreQueuedToDiskAfterDelay)
        {
            m_queue->SetWriteToStorageIdleLimits(100ms, 10);
            m_queue->EnableQueuingToStorage();
            auto result = m_queue->QueueEventToStorage(GenerateSamplePayload());
            Assert::AreNotEqual(0, (int)result, L"Didn't get a token back from queueing the event");
            Assert::AreEqual(1, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Incorrect number of items");

            AsyncHelper::RunSynced(create_task([] {
                std::this_thread::sleep_for(200ms);
            }));

            Assert::AreEqual(0, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Shouldn't find items waiting to be written to disk");
            Assert::AreEqual(1, (int)AsyncHelper::RunSynced(this->GetCurrentFileCountInQueueFolder()), L"Incorrect file count found");
        }

        TEST_METHOD(ItemsAreQueuedToDiskAfterThreshold)
        {
            m_queue->SetWriteToStorageIdleLimits(1000ms, 10);
            m_queue->EnableQueuingToStorage();

            this_thread::sleep_for(50ms);
            
            for (int i = 0; i < 11; i++)
            {
                m_queue->QueueEventToStorage(GenerateSamplePayload());
            }

            AsyncHelper::RunSynced(create_task([] {
                std::this_thread::sleep_for(500ms);
            }));

            Assert::AreEqual(0, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Shouldn't find items waiting to be written to disk");
            Assert::AreEqual(11, (int)AsyncHelper::RunSynced(this->GetCurrentFileCountInQueueFolder()), L"Incorrect file count found");
        }

        TEST_METHOD(QueuingAfterShutdownDoesntAddToQueue)
        {
            m_queue->EnableQueuingToStorage();

            auto result = m_queue->QueueEventToStorage(GenerateSamplePayload());
            Assert::AreNotEqual(0, (int)result, L"Didn't get a token back from queueing the event");
            Assert::AreEqual(1, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Incorrect number of items");

            AsyncHelper::RunSynced(m_queue->PersistAllQueuedItemsToStorageAndShutdown());

            Assert::AreEqual(0, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Shouldn't find items waiting to be written to disk");

            result = m_queue->QueueEventToStorage(GenerateSamplePayload());

            Assert::AreEqual(0, (int)result, L"Didn't expect to get a payload back");
            Assert::AreEqual(0, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Shouldn't find items waiting to be written to disk");
        }

        TEST_METHOD(MultipleItemsAreQueuedToDisk)
        {
            m_queue->EnableQueuingToStorage();
            m_queue->QueueEventToStorage(GenerateSamplePayload());
            m_queue->QueueEventToStorage(GenerateSamplePayload());
            Assert::AreEqual(2, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Incorrect number of items");

            AsyncHelper::RunSynced(m_queue->PersistAllQueuedItemsToStorageAndShutdown());

            Assert::AreEqual(0, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Shouldn't find items waiting to be written to disk");
            Assert::AreEqual(2, (int)AsyncHelper::RunSynced(this->GetCurrentFileCountInQueueFolder()), L"Incorrect file count found");
        }

        TEST_METHOD(ItemContentStoredInCorrectFile)
        {
            m_queue->EnableQueuingToStorage();
            JsonObject^ payload = GenerateSamplePayload();

            auto result = m_queue->QueueEventToStorage(payload);
            Assert::AreNotEqual(0, (int)result, L"Didn't get a token back from queueing the event");
            Assert::AreEqual(1, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Incorrect number of items");

            AsyncHelper::RunSynced(m_queue->PersistAllQueuedItemsToStorageAndShutdown());

            Assert::AreEqual(0, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Shouldn't find items waiting to be written to disk");
            Assert::AreEqual(1, (int)AsyncHelper::RunSynced(this->GetCurrentFileCountInQueueFolder()), L"Incorrect file count found");

            JsonObject^ fromFile = AsyncHelper::RunSynced(this->RetrievePayloadForId(result));
            Assert::IsNotNull(fromFile, L"Expected payload to be found, and loaded");
            if (fromFile == nullptr)
            {
                return;
            }

            for (auto&& item : payload)
            {
                Assert::IsTrue(fromFile->HasKey(item->Key), (L"Key: " + item->Key + L" not found")->Data());
            }
        }

        TEST_METHOD(AllItemsRemovedFromStorageWhenQueueIsCleared)
        {
            m_queue->EnableQueuingToStorage();
            m_queue->QueueEventToStorage(GenerateSamplePayload());
            m_queue->QueueEventToStorage(GenerateSamplePayload());
            Assert::AreEqual(2, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Incorrect number of items");
            Assert::AreEqual(0, (int)this->GetWrittenItemsSize(), L"Didn't expect any items to successfully write");

            AsyncHelper::RunSynced(m_queue->PersistAllQueuedItemsToStorageAndShutdown());

            Assert::AreEqual(2, (int)this->GetWrittenItemsSize(), L"Should've have anything in the successfully written queue.");
            Assert::AreEqual(2, (int)AsyncHelper::RunSynced(this->GetCurrentFileCountInQueueFolder()), L"Incorrect file count found");

            AsyncHelper::RunSynced(m_queue->Clear());

            Assert::AreEqual(0, (int)AsyncHelper::RunSynced(this->GetCurrentFileCountInQueueFolder()), L"Didn't expect any files");
        }

        TEST_METHOD(QueuingLowPriorityItemsDoesntTriggerWritingToDisk)
        {
            m_queue->SetWriteToStorageIdleLimits(50ms, 4);
            m_queue->EnableQueuingToStorage();

            this_thread::sleep_for(100ms);

            m_queue->QueueEventToStorage(GenerateSamplePayload(), EventPriority::Low);
            m_queue->QueueEventToStorage(GenerateSamplePayload(), EventPriority::Low);
            m_queue->QueueEventToStorage(GenerateSamplePayload(), EventPriority::Low);
            m_queue->QueueEventToStorage(GenerateSamplePayload(), EventPriority::Low);

            this_thread::sleep_for(100ms);

            Assert::AreEqual(4, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Incorrect number of items");
            m_queue->Clear();
        }

        TEST_METHOD(QueueCanBeRestoredFromStorage)
        {
            auto item1 = GenerateSamplePayload();
            auto item2 = GenerateSamplePayload();
            auto item3 = GenerateSamplePayload();

            AsyncHelper::RunSynced(this->WritePayload(1, item1));
            AsyncHelper::RunSynced(this->WritePayload(2, item2));
            AsyncHelper::RunSynced(this->WritePayload(3, item3));

            auto queue = AsyncHelper::RunSynced(EventStorageQueue::LoadItemsFromStorage(m_queueFolder));
            Assert::AreEqual(3, (int)queue.size(), L"Expected items in the successfully written queue");
        }

        TEST_METHOD(QueueCanBeRestoredFromStorageWithEmptyItem)
        {
            auto item1 = GenerateSamplePayload();
            auto item2 = GenerateSamplePayload();
            auto item3 = GenerateSamplePayload();

            AsyncHelper::RunSynced(this->WritePayload(1, item1));
            AsyncHelper::RunSynced(this->WritePayload(2, item2));
            AsyncHelper::RunSynced(this->WritePayload(3, item3));
            AsyncHelper::RunSynced(this->WriteEmptyPayload(4));

            auto queue = AsyncHelper::RunSynced(EventStorageQueue::LoadItemsFromStorage(m_queueFolder));
            Assert::AreEqual(3, (int)queue.size(), L"Expected items in the successfully written queue");
        }

        TEST_METHOD(ItemsCanBeRemovedFromStorage)
        {
            m_queue->SetWriteToStorageIdleLimits(10ms, 1);
            m_queue->EnableQueuingToStorage();
            auto result = m_queue->QueueEventToStorage(GenerateSamplePayload());
            Assert::AreNotEqual(0, (int)result, L"Didn't get a token back from queueing the event");
            Assert::AreEqual(1, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Incorrect number of items");

            this_thread::sleep_for(50ms);

            Assert::AreEqual(0, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Shouldn't find items waiting to be written to disk");
            Assert::AreEqual(1, (int)AsyncHelper::RunSynced(this->GetCurrentFileCountInQueueFolder()), L"Incorrect file count found");

            AsyncHelper::RunSynced(m_queue->RemoveEventFromStorage(*(m_writtenItems.front())));

            Assert::AreEqual(0, (int)AsyncHelper::RunSynced(this->GetCurrentFileCountInQueueFolder()), L"Item wasn't deleted");
        }
        
        task<unsigned int> GetCurrentFileCountInQueueFolder()
        {
            auto files = co_await m_queueFolder->GetFilesAsync();
            return files->Size;
        }

        task<void> WritePayload(long long id, JsonObject^ payload)
        {
            auto fileName = GetFileNameForId(id);
            auto file = co_await m_queueFolder->CreateFileAsync(fileName, CreationCollisionOption::ReplaceExisting);
            co_await FileIO::WriteTextAsync(file, payload->Stringify());
        }

        task<void> WriteEmptyPayload(long long id)
        {
            auto fileName = GetFileNameForId(id);
            auto file = co_await m_queueFolder->CreateFileAsync(fileName, CreationCollisionOption::ReplaceExisting);
            co_await FileIO::WriteTextAsync(file, L"");
        }

        task<JsonObject^> RetrievePayloadForId(long long id)
        {
            auto fileName = ref new String(std::to_wstring(id).append(L".json").c_str());

            StorageFile^ file = dynamic_cast<StorageFile^>(co_await m_queueFolder->TryGetItemAsync(fileName));

            if (file == nullptr)
            {
                return nullptr;
            }
            
            auto fileContent = co_await FileIO::ReadTextAsync(file);
            auto result = JsonObject::Parse(fileContent);

            return result;
        }
    };
}
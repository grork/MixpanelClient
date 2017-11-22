#include "pch.h"
#include <memory>

#include "CppUnitTest.h"
#include "AsyncHelper.h"
#include "EventQueue.h"

using namespace Platform;
using namespace std;
using namespace CodevoidN::Tests::Utilities;
using namespace CodevoidN::Utilities::Mixpanel;
using namespace concurrency;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Windows::Data::Json;
using namespace Windows::Foundation::Collections;
using namespace Windows::Storage;

namespace CodevoidN { namespace  Tests { namespace Mixpanel
{
    static task<StorageFolder^> GetAndClearTestFolder()
    {
        auto storageFolder = co_await ApplicationData::Current->LocalFolder->CreateFolderAsync("EventQueueTests",
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

    TEST_CLASS(EventQueueTests)
    {
    private:
        shared_ptr<EventQueue> m_queue;
        StorageFolder^ m_queueFolder;

    public:
        TEST_METHOD_INITIALIZE(InitializeClass)
        {
            m_queueFolder = AsyncHelper::RunSynced(GetAndClearTestFolder());
            m_queue = make_shared<EventQueue>(m_queueFolder);
            m_queue->DisableQueuingToStorage();
        }

        TEST_METHOD(ConstructionThrowsIfNoFolderProvided)
        {
            bool exceptionSeen = false;
            try
            {
                auto result = make_unique<EventQueue>(nullptr);
            }
            catch (const invalid_argument&)
            {
                exceptionSeen = true;
            }

            Assert::IsTrue(exceptionSeen, L"Expected to get exception on construction");
        }

        TEST_METHOD(CanQueueEvent)
        {
            auto result = m_queue->QueueEventForUpload(GenerateSamplePayload());
            Assert::AreNotEqual(0, (int)result, L"Didn't get a token back from queueing the event");
            Assert::AreEqual(1, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Incorrect number of items");
        }

        TEST_METHOD(CanClearQueue)
        {
            m_queue->QueueEventForUpload(GenerateSamplePayload());
            m_queue->QueueEventForUpload(GenerateSamplePayload());
            Assert::AreEqual(2, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Incorrect number of items");

            AsyncHelper::RunSynced(m_queue->Clear());
            Assert::AreEqual(0, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Expected queue to be clear");
        }

        TEST_METHOD(ItemsAreQueuedToDisk)
        {
            m_queue->EnableQueuingToStorage();
            auto result = m_queue->QueueEventForUpload(GenerateSamplePayload());
            Assert::AreNotEqual(0, (int)result, L"Didn't get a token back from queueing the event");
            Assert::AreEqual(1, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Incorrect number of items");

            AsyncHelper::RunSynced(m_queue->PersistAllQueuedItemsToStorageAndShutdown());

            Assert::AreEqual(0, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Shouldn't find items waiting to be written to disk");
            Assert::AreEqual(1, (int)AsyncHelper::RunSynced(this->GetCurrentFileCountInQueueFolder()), L"Incorrect file count found");
        }

        TEST_METHOD(QueuingAfterShutdownDoesntAddToQueue)
        {
            m_queue->EnableQueuingToStorage();

            auto result = m_queue->QueueEventForUpload(GenerateSamplePayload());
            Assert::AreNotEqual(0, (int)result, L"Didn't get a token back from queueing the event");
            Assert::AreEqual(1, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Incorrect number of items");

            AsyncHelper::RunSynced(m_queue->PersistAllQueuedItemsToStorageAndShutdown());

            Assert::AreEqual(0, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Shouldn't find items waiting to be written to disk");

            result = m_queue->QueueEventForUpload(GenerateSamplePayload());

            Assert::AreEqual(0, (int)result, L"Didn't expect to get a payload back");
            Assert::AreEqual(0, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Shouldn't find items waiting to be written to disk");
        }

        TEST_METHOD(MultipleItemsAreQueuedToDisk)
        {
            m_queue->EnableQueuingToStorage();
            m_queue->QueueEventForUpload(GenerateSamplePayload());
            m_queue->QueueEventForUpload(GenerateSamplePayload());
            Assert::AreEqual(2, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Incorrect number of items");

            AsyncHelper::RunSynced(m_queue->PersistAllQueuedItemsToStorageAndShutdown());

            Assert::AreEqual(0, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Shouldn't find items waiting to be written to disk");
            Assert::AreEqual(2, (int)AsyncHelper::RunSynced(this->GetCurrentFileCountInQueueFolder()), L"Incorrect file count found");
        }

        TEST_METHOD(ItemContentStoredInCorrectFile)
        {
            m_queue->EnableQueuingToStorage();
            JsonObject^ payload = GenerateSamplePayload();

            auto result = m_queue->QueueEventForUpload(payload);
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
            m_queue->QueueEventForUpload(GenerateSamplePayload());
            m_queue->QueueEventForUpload(GenerateSamplePayload());
            Assert::AreEqual(2, (int)m_queue->GetWaitingToWriteToStorageLength(), L"Incorrect number of items");
            Assert::AreEqual(0, (int)m_queue->GetWaitingForUploadLength(), L"Didn't expect any items to upload");

            AsyncHelper::RunSynced(m_queue->PersistAllQueuedItemsToStorageAndShutdown());

            Assert::AreEqual(0, (int)m_queue->GetWaitingForUploadLength(), L"Shouldn't have anything in the upload queue.");
            Assert::AreEqual(2, (int)AsyncHelper::RunSynced(this->GetCurrentFileCountInQueueFolder()), L"Incorrect file count found");

            AsyncHelper::RunSynced(m_queue->Clear());

            Assert::AreEqual(0, (int)AsyncHelper::RunSynced(this->GetCurrentFileCountInQueueFolder()), L"Didn't expect any files");
        }

        TEST_METHOD(QueueCanBeRestoredFromStorage)
        {
            auto item1 = GenerateSamplePayload();
            auto item2 = GenerateSamplePayload();
            auto item3 = GenerateSamplePayload();

            AsyncHelper::RunSynced(this->WritePayload(1, item1));
            AsyncHelper::RunSynced(this->WritePayload(2, item2));
            AsyncHelper::RunSynced(this->WritePayload(3, item3));

            shared_ptr<EventQueue> queue = AsyncHelper::RunSynced(this->GetQueueFromStorage());
            Assert::AreEqual(0, (int)queue->GetWaitingToWriteToStorageLength(), L"Expected no items waiting to write to storage");
            Assert::AreEqual(3, (int)queue->GetWaitingForUploadLength(), L"Expected items in the upload queue");
        }

        task<shared_ptr<EventQueue>> GetQueueFromStorage()
        {
            auto queue = make_shared<EventQueue>(m_queueFolder);
            co_await queue->RestorePendingUploadQueueFromStorage();

            return queue;
        }
        
        task<unsigned int> GetCurrentFileCountInQueueFolder()
        {
            auto files = co_await m_queueFolder->GetFilesAsync();
            return files->Size;
        }

        task<void> WritePayload(long long id, JsonObject^ payload)
        {
            auto fileName = ref new String(std::to_wstring(id).append(L".json").c_str());
            auto file = co_await m_queueFolder->CreateFileAsync(fileName, CreationCollisionOption::ReplaceExisting);
            co_await FileIO::WriteTextAsync(file, payload->Stringify());
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
} } }
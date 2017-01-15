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
            auto result = m_queue->QueueEvent(GenerateSamplePayload());
            Assert::IsFalse(0 == result, L"Didn't get a token back from queueing the event");
            Assert::IsTrue(1 == m_queue->GetQueueLength(), L"Incorrect number of items");
        }

        TEST_METHOD(CanRemoveEvent)
        {
            auto result = m_queue->QueueEvent(GenerateSamplePayload());
            Assert::IsFalse(0 == result, L"Didn't get a token back from queueing the event");
            Assert::IsTrue(1 == m_queue->GetQueueLength(), L"Incorrect number of items");
            
            m_queue->RemoveEvent(result);

            Assert::IsTrue(0 == m_queue->GetQueueLength(), L"Expected 0 items in the list");
        }

        TEST_METHOD(CanClearQueue)
        {
            m_queue->QueueEvent(GenerateSamplePayload());
            m_queue->QueueEvent(GenerateSamplePayload());
            Assert::AreEqual(2, (int)m_queue->GetQueueLength(), L"Incorrect number of items");

            m_queue->Clear();
            Assert::AreEqual(0, (int)m_queue->GetQueueLength(), L"Expected queue to be clear");
        }

        TEST_METHOD(ItemsAreQueuedToDisk)
        {
            m_queue->EnableQueuingToStorage();
            auto result = m_queue->QueueEvent(GenerateSamplePayload());
            Assert::IsFalse(0 == result, L"Didn't get a token back from queueing the event");
            Assert::IsTrue(1 == m_queue->GetQueueLength(), L"Incorrect number of items");

            Assert::AreEqual(1, (int)AsyncHelper::RunSynced(this->GetCurrentFileCountInQueueFolder()), L"Incorrect file count found");
        }

        TEST_METHOD(MultipleItemsAreQueuedToDisk)
        {
            m_queue->EnableQueuingToStorage();
            m_queue->QueueEvent(GenerateSamplePayload());
            m_queue->QueueEvent(GenerateSamplePayload());
            Assert::AreEqual(2, (int)m_queue->GetQueueLength(), L"Incorrect number of items");

            Assert::AreEqual(2, (int)AsyncHelper::RunSynced(this->GetCurrentFileCountInQueueFolder()), L"Incorrect file count found");
        }

        TEST_METHOD(ItemContentStoredInCorrectFile)
        {
            m_queue->EnableQueuingToStorage();
            JsonObject^ payload = GenerateSamplePayload();

            auto result = m_queue->QueueEvent(payload);
            Assert::IsFalse(0 == result, L"Didn't get a token back from queueing the event");
            Assert::IsTrue(1 == m_queue->GetQueueLength(), L"Incorrect number of items");

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

        TEST_METHOD(ItemRemovedFromLocalStorageWhenRemovedFromQueue)
        {
            m_queue->EnableQueuingToStorage();
            JsonObject^ payload = GenerateSamplePayload();

            auto result = m_queue->QueueEvent(payload);
            JsonObject^ fromFile = AsyncHelper::RunSynced(this->RetrievePayloadForId(result));
            Assert::IsNotNull(fromFile, L"Expected payload to be found, and loaded");

            m_queue->RemoveEvent(result);

            fromFile = AsyncHelper::RunSynced(this->RetrievePayloadForId(result));
            Assert::IsNull(fromFile, L"Expected File to be removed");
        }

        TEST_METHOD(AllItemsRemovedFromStorageWhenQueueIsCleared)
        {
            m_queue->EnableQueuingToStorage();
            m_queue->QueueEvent(GenerateSamplePayload());
            m_queue->QueueEvent(GenerateSamplePayload());
            Assert::AreEqual(2, (int)m_queue->GetQueueLength(), L"Incorrect number of items");

            Assert::AreEqual(2, (int)AsyncHelper::RunSynced(this->GetCurrentFileCountInQueueFolder()), L"Incorrect file count found");

            m_queue->Clear();

            Assert::AreEqual(0, (int)AsyncHelper::RunSynced(this->GetCurrentFileCountInQueueFolder()), L"Didn't expect any files");
        }

        task<unsigned int> GetCurrentFileCountInQueueFolder()
        {
            auto files = co_await m_queueFolder->GetFilesAsync();
            return files->Size;
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
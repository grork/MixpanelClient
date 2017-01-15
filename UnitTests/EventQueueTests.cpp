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
            this->m_queueFolder = AsyncHelper::RunSynced(GetAndClearTestFolder());
            this->m_queue = make_shared<EventQueue>(m_queueFolder);
            this->m_queue->DisableQueuingToStorage();
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
            auto result = this->m_queue->QueueEvent(GenerateSamplePayload());
            Assert::IsFalse(0 == result, L"Didn't get a token back from queueing the event");
            Assert::IsTrue(1 == this->m_queue->GetQueueLength(), L"Incorrect number of items");
        }

        TEST_METHOD(CanRemoveEvent)
        {
            auto result = this->m_queue->QueueEvent(GenerateSamplePayload());
            Assert::IsFalse(0 == result, L"Didn't get a token back from queueing the event");
            Assert::IsTrue(1 == this->m_queue->GetQueueLength(), L"Incorrect number of items");
            
            this->m_queue->RemoveEvent(result);

            Assert::IsTrue(0 == this->m_queue->GetQueueLength(), L"Expected 0 items in the list");
        }

        TEST_METHOD(ItemsAreQueuedToDisk)
        {
            this->m_queue->EnableQueuingToStorage();
            auto result = this->m_queue->QueueEvent(GenerateSamplePayload());
            Assert::IsFalse(0 == result, L"Didn't get a token back from queueing the event");
            Assert::IsTrue(1 == this->m_queue->GetQueueLength(), L"Incorrect number of items");

            Assert::AreEqual((int)1, (int)AsyncHelper::RunSynced(this->CorrectCountInStorageFolder(1)), L"Incorrect file count found");
        }

        TEST_METHOD(ItemContentStoredInCorrectFile)
        {
            this->m_queue->EnableQueuingToStorage();
            JsonObject^ payload = GenerateSamplePayload();

            auto result = this->m_queue->QueueEvent(payload);
            Assert::IsFalse(0 == result, L"Didn't get a token back from queueing the event");
            Assert::IsTrue(1 == this->m_queue->GetQueueLength(), L"Incorrect number of items");

            Assert::AreEqual((int)1, (int)AsyncHelper::RunSynced(this->CorrectCountInStorageFolder(1)), L"Incorrect file count found");

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
            this->m_queue->EnableQueuingToStorage();
            JsonObject^ payload = GenerateSamplePayload();

            auto result = this->m_queue->QueueEvent(payload);
            JsonObject^ fromFile = AsyncHelper::RunSynced(this->RetrievePayloadForId(result));
            Assert::IsNotNull(fromFile, L"Expected payload to be found, and loaded");

            this->m_queue->RemoveEvent(result);

            fromFile = AsyncHelper::RunSynced(this->RetrievePayloadForId(result));
            Assert::IsNull(fromFile, L"Expected File to be removed");
        }

        task<unsigned int> CorrectCountInStorageFolder(unsigned int expectedFileCount)
        {
            auto files = co_await m_queueFolder->GetFilesAsync();
            return files->Size;
        }

        task<JsonObject^> RetrievePayloadForId(long long id)
        {
            auto fileName = ref new String(std::to_wstring(id).append(L".json").c_str());

            StorageFile^ file = dynamic_cast<StorageFile^>(co_await this->m_queueFolder->TryGetItemAsync(fileName));

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
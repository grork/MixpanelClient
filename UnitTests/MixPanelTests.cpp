#include "pch.h"
#include "CppUnitTest.h"
#include "MixpanelClient.h"
#include "AsyncHelper.h"

using namespace std;
using namespace std::chrono;
using namespace Platform;
using namespace Platform::Collections;
using namespace CodevoidN::Tests::Utilities;
using namespace CodevoidN::Utilities::Mixpanel;
using namespace concurrency;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Windows::Data::Json;
using namespace Windows::Foundation::Collections;
using namespace Windows::Storage;
using namespace Windows::Web::Http::Headers;

// Set to 1, and an actual token to test sending
// request to the service.
#define DEFAULT_TOKEN_SET 0
#define DEFAULT_TOKEN L"DEFAULT_TOKEN"

#define OVERRIDE_STORAGE_FOLDER L"MixpanelClientTests"
constexpr milliseconds DEFAULT_IDLE_TIMEOUT = 10ms;

namespace CodevoidN { namespace  Tests { namespace Mixpanel
{
    TEST_CLASS(MixpanelTests)
    {
    private:
        MixpanelClient^ m_client;

        static task<StorageFolder^> GetAndClearTestFolder()
        {
            auto storageFolder = co_await ApplicationData::Current->LocalFolder->CreateFolderAsync(OVERRIDE_STORAGE_FOLDER,
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

		static vector<IJsonValue^> CaptureRequestPayloads(IMap<String^, IJsonValue^>^ payload)
		{
			// Data is intended in the 'data' keyed item in the payload.
			// Assume it's a JsonArray...
			JsonArray^ data = dynamic_cast<JsonArray^>(payload->Lookup(L"data"));
			vector<IJsonValue^> items;

			// Copy into a vector for easier access.
			for (unsigned int i = 0; i < data->Size; i++)
			{
				items.push_back(data->GetAt(i));
			}

            return items;
		}

    public:
        TEST_METHOD_INITIALIZE(InitializeClass)
        {
			m_client = ref new MixpanelClient(DEFAULT_TOKEN);

            // Disable Persistence of Super properties
            // to help maintain these tests as stateless
            m_client->PersistSuperPropertiesToApplicationData = false;

            auto folder = AsyncHelper::RunSynced(GetAndClearTestFolder());

            // The URL here is a helpful endpoint on the internet that just round-files
            // the requests to enable simple testing.
            m_client->Initialize(folder, ref new Uri(L"https://jsonplaceholder.typicode.com/posts"));
			m_client->ConfigureForTesting(DEFAULT_IDLE_TIMEOUT, 10);

            // Set the default service mock to avoid sending things to the
            // internet when we don't really need to.
            m_client->SetUploadToServiceMock([](auto uri, auto payload, auto)
            {
                return task_from_result(true);
            });
        }

#pragma region Tracking Events and Super Properties
        TEST_METHOD_CLEANUP(CleanupClass)
        {
            if (m_client == nullptr)
            {
                return;
            }

            AsyncHelper::RunSynced(m_client->Shutdown());
        }

        TEST_METHOD(TrackThrowsWithMissingEventName)
        {
            bool exceptionThrown = false;

            try
            {
                m_client->Track(nullptr, ref new ValueSet());
            }
            catch (InvalidArgumentException^ ex)
            {
                exceptionThrown = true;
            }

            Assert::IsTrue(exceptionThrown, L"Didn't get expected exception");
        }

        TEST_METHOD(TrackThrowsIfNotInitialized)
        {
            bool exceptionThrown = false;
            auto client = ref new MixpanelClient(DEFAULT_TOKEN);

            try
            {
                client->Track("Faux", ref new ValueSet());
            }
            catch (InvalidArgumentException^ ex)
            {
                exceptionThrown = true;
            }

            Assert::IsTrue(exceptionThrown, L"Didn't get expected exception");
        }

        TEST_METHOD(GeneratingJsonObjectDoesntThrowForSupportedTypes)
        {
            IPropertySet^ properties = ref new PropertySet();
            properties->Insert(L"StringValue", L"Value");
            properties->Insert(L"IntValue", 42);
            properties->Insert(L"DoubleValue", 4.0);
            properties->Insert(L"FloatValue", 4.0f);
            properties->Insert(L"BooleanValue", true);
            auto calendar = ref new Windows::Globalization::Calendar();
            properties->Insert(L"DateTimeValue", calendar->GetDateTime());

            JsonObject^ result = ref new JsonObject();
            try
            {
                MixpanelClient::AppendPropertySetToJsonPayload(properties, result);
            }
            catch (...)
            {
                // Thanks C++ unit testing, for not catching
                // WinRT exceptions
                Assert::Fail(L"Didn't Expect exception");
            }
        }

        TEST_METHOD(GeneratingJsonObjectThrowsIfPropertySetIncludesUnsupportedType)
        {
            IPropertySet^ properties = ref new PropertySet();
            properties->Insert(L"StringValue", L"Value");
            properties->Insert(L"IntValue", 42);
            properties->Insert(L"DoubleValue", 4.0);
            properties->Insert(L"FloatValue", 4.0f);
            properties->Insert(L"BooleanValue", true);
            properties->Insert(L"BadValue", ref new Windows::Foundation::Uri(L"http://foo.com"));

            JsonObject^ result = ref new JsonObject();
            bool exceptionThrown = false;
            try
            {
                MixpanelClient::AppendPropertySetToJsonPayload(properties, result);
            }
            catch (InvalidCastException^ ex)
            {
                exceptionThrown = true;
            }

            Assert::IsTrue(exceptionThrown, L"Didn't get exception for non-valuetype in property set");
        }

        TEST_METHOD(CorrectJsonValuesAreGeneratedForSupportedTypes)
        {
            IPropertySet^ properties = ref new PropertySet();
            properties->Insert(L"StringValue", L"Value");
            properties->Insert(L"IntValue", 42);
            properties->Insert(L"DoubleValue", 4.1);
            properties->Insert(L"FloatValue", 4.2f);
            properties->Insert(L"BooleanValue", true);
            auto calendar = ref new Windows::Globalization::Calendar();
            auto insertedDateTime = calendar->GetDateTime();
            properties->Insert(L"DateTimeValue", insertedDateTime);

            auto result = ref new JsonObject();
            MixpanelClient::AppendPropertySetToJsonPayload(properties, result);

            // Validate StringValue is present, and matches
            Assert::IsTrue(result->HasKey(L"StringValue"), L"StringValue key not present in JSON");
            auto stringValue = result->GetNamedString(L"StringValue");
            Assert::AreEqual(L"Value", stringValue, L"Inserted string values didn't match");

            // Validate that IntValue is present, and matches
            Assert::IsTrue(result->HasKey(L"IntValue"), L"IntValue not present");
            int number = static_cast<int>(result->GetNamedNumber(L"IntValue"));
            Assert::AreEqual(42, number, L"IntValue doesn't match");

            // Validate that that DoubleValue is present, and matches
            Assert::IsTrue(result->HasKey(L"DoubleValue"), L"DoubleValue not present");
            double number2 = static_cast<double>(result->GetNamedNumber(L"DoubleValue"));
            Assert::AreEqual(4.1, number2, L"DoubleValue didn't match");

            // Validate that FloatValue is present, and matches
            Assert::IsTrue(result->HasKey(L"FloatValue"), L"FloatValue key isn't present");
            float number3 = static_cast<float>(result->GetNamedNumber(L"FloatValue"));
            Assert::AreEqual(4.2f, number3, L"FloatValue didn't match");

            // Validate that BooleanValue is present and matches
            Assert::IsTrue(result->HasKey(L"BooleanValue"), L"BooleanValue key isn't present");
            bool truthy = static_cast<bool>(result->GetNamedBoolean(L"BooleanValue"));
            Assert::AreEqual(true, truthy, L"BooleanValue didn't match");

            // Validate that DateTimeValue is Present and matches
            Assert::IsTrue(result->HasKey(L"DateTimeValue"), L"DateTimee key isn't present");
            double dateTime = static_cast<double>(result->GetNamedNumber(L"DateTimeValue"));
            Assert::AreEqual(static_cast<double>(WindowsTickToUnixSeconds(insertedDateTime.UniversalTime)), dateTime, L"DateTimeValue didn't match");
        }

        TEST_METHOD(ExceptionThrownWhenIncludingMpPrefixInPropertySet)
        {
            IPropertySet^ properties = ref new PropertySet();
            properties->Insert(L"mp_Foo", L"Value");

            bool exceptionThrown = false;
            JsonObject^ result = ref new JsonObject();

            try
            {
                MixpanelClient::AppendPropertySetToJsonPayload(properties, result);
            }
            catch (InvalidArgumentException^ ex)
            {
                exceptionThrown = true;
            }

            Assert::IsTrue(exceptionThrown, L"Didn't get exception for mp_ prefixed property in property set");
        }

        TEST_METHOD(TrackingPayloadIncludesTokenAndPayload)
        {
            IPropertySet^ properties = ref new PropertySet();
            properties->Insert(L"StringValue", L"Value");

            auto trackPayload = m_client->GenerateTrackingJsonPayload(L"TestEvent", properties);

            // Check that the event data is correct
            Assert::IsTrue(trackPayload->HasKey(L"event"), L"Didn't have event key");
            Assert::AreEqual(L"TestEvent", trackPayload->GetNamedString("event"), L"Event name incorrect");

            // Check that the actual properties we passed in are present
            Assert::IsTrue(trackPayload->HasKey(L"properties"), L"No properties payload");
            auto propertiesPayload = trackPayload->GetNamedObject("properties");

            // Validate StringValue is present, and matches
            Assert::IsTrue(propertiesPayload->HasKey(L"StringValue"), L"StringValue key not present in JSON");
            auto stringValue = propertiesPayload->GetNamedString(L"StringValue");
            Assert::AreEqual(L"Value", stringValue, L"Inserted string values didn't match");

            // Validate that the API Token is present
            Assert::IsTrue(propertiesPayload->HasKey(L"token"), L"No token in properties payload");
            Assert::AreEqual(DEFAULT_TOKEN, propertiesPayload->GetNamedString(L"token"), L"Token had incorrect value");
        }

        TEST_METHOD(TrackingPayloadIncludesTokenAndSuperPropertiesPayload)
        {
            IPropertySet^ properties = ref new PropertySet();
            properties->Insert(L"StringValue", L"Value");
            m_client->SetSuperProperty(L"SuperPropertyA", L"SuperValueA");
            m_client->SetSuperProperty(L"SuperPropertyB", 7.0);
            m_client->SetSuperProperty(L"SuperPropertyC", true);

            auto trackPayload = m_client->GenerateTrackingJsonPayload(L"TestEvent", properties);

            // Check that the event data is correct
            Assert::IsTrue(trackPayload->HasKey(L"event"), L"Didn't have event key");
            Assert::AreEqual(L"TestEvent", trackPayload->GetNamedString("event"), L"Event name incorrect");

            // Check that the actual properties we passed in are present
            Assert::IsTrue(trackPayload->HasKey(L"properties"), L"No properties payload");
            auto propertiesPayload = trackPayload->GetNamedObject("properties");

            // Validate StringValue is present, and matches
            Assert::IsTrue(propertiesPayload->HasKey(L"StringValue"), L"StringValue key not present in JSON");
            auto stringValue = propertiesPayload->GetNamedString(L"StringValue");
            Assert::AreEqual(L"Value", stringValue, L"Inserted string values didn't match");

            // Validate that the API Token is present
            Assert::IsTrue(propertiesPayload->HasKey(L"token"), L"No token in properties payload");
            Assert::AreEqual(DEFAULT_TOKEN, propertiesPayload->GetNamedString(L"token"), L"Token had incorrect value");

            // Validate that Super Property A is present
            Assert::IsTrue(propertiesPayload->HasKey(L"SuperPropertyA"), L"No SuperPropertyA in properties payload");
            Assert::AreEqual(L"SuperValueA", propertiesPayload->GetNamedString(L"SuperPropertyA"), L"SuperPropertyA had incorrect value");

            // Validate that Super Property B is present
            Assert::IsTrue(propertiesPayload->HasKey(L"SuperPropertyB"), L"No SuperPropertyB in properties payload");
            Assert::AreEqual(7.0, propertiesPayload->GetNamedNumber(L"SuperPropertyB"), L"SuperPropertyB had incorrect value");

            // Validate that Super Property C is present
            Assert::IsTrue(propertiesPayload->HasKey(L"SuperPropertyC"), L"No SuperPropertyC in properties payload");
            Assert::AreEqual(true, propertiesPayload->GetNamedBoolean(L"SuperPropertyC"), L"SuperPropertyC had incorrect value");
        }

        TEST_METHOD(CanSetSuperPropertyMoreThanOnce)
        {
            IPropertySet^ properties = ref new PropertySet();
            m_client->SetSuperProperty(L"SuperPropertyA", L"SuperValueA");

            auto trackPayload = m_client->GenerateTrackingJsonPayload(L"TestEvent", properties);

            // Check that the actual properties we passed in are present
            Assert::IsTrue(trackPayload->HasKey(L"properties"), L"No properties payload");
            auto propertiesPayload = trackPayload->GetNamedObject("properties");

            // Validate that Super Property is present
            Assert::IsTrue(propertiesPayload->HasKey(L"SuperPropertyA"), L"No SuperPropertyA in properties payload");
            Assert::AreEqual(L"SuperValueA", propertiesPayload->GetNamedString(L"SuperPropertyA"), L"SuperPropertyA had incorrect value");

            // Set the super property a second time
            m_client->SetSuperProperty(L"SuperPropertyA", L"DifferentValue");

            // Validate payload again
            trackPayload = m_client->GenerateTrackingJsonPayload(L"TestEvent", properties);
            propertiesPayload = trackPayload->GetNamedObject("properties");

            // Validate that Super Property is present
            Assert::IsTrue(propertiesPayload->HasKey(L"SuperPropertyA"), L"No SuperPropertyA in properties payload");
            Assert::AreEqual(L"DifferentValue", propertiesPayload->GetNamedString(L"SuperPropertyA"), L"SuperPropertyA had incorrect value");
        }

        TEST_METHOD(ClearingSuperPropertiesWorks)
        {
            IPropertySet^ properties = ref new PropertySet();
            m_client->SetSuperProperty(L"SuperPropertyA", L"SuperValueA");

            auto trackPayload = m_client->GenerateTrackingJsonPayload(L"TestEvent", properties);

            // Check that the actual properties we passed in are present
            Assert::IsTrue(trackPayload->HasKey(L"properties"), L"No properties payload");
            auto propertiesPayload = trackPayload->GetNamedObject("properties");

            // Validate that Super Property is present
            Assert::IsTrue(propertiesPayload->HasKey(L"SuperPropertyA"), L"No SuperPropertyA in properties payload");
            Assert::AreEqual(L"SuperValueA", propertiesPayload->GetNamedString(L"SuperPropertyA"), L"SuperPropertyA had incorrect value");

            // Clear the super properties, and generate the payload again
            m_client->ClearSuperProperties();

            // Validate payload again
            trackPayload = m_client->GenerateTrackingJsonPayload(L"TestEvent", properties);
            propertiesPayload = trackPayload->GetNamedObject("properties");

            // Validate that Super Property isn't present
            Assert::IsFalse(propertiesPayload->HasKey(L"SuperPropertyA"), L"SuperPropertyA present, when it should have been cleared");
        }

        TEST_METHOD(CanCheckForSuperPropertyWhenNotSet)
        {
            Assert::IsFalse(m_client->HasSuperProperty(L"SuperPropertyA"), L"SuperPropertyA shouldn't have been in the list");
        }

        TEST_METHOD(CanCheckForSuperPropertyWhenSet)
        {
            m_client->SetSuperProperty(L"SuperPropertyA", L"SuperValueA");
            Assert::IsTrue(m_client->HasSuperProperty(L"SuperPropertyA"), L"SuperPropertyA not in list");
        }

        TEST_METHOD(CanReadBackSuperProperties)
        {
            m_client->SetSuperProperty(L"SuperPropertyA", L"SuperValueA");
            m_client->SetSuperProperty(L"SuperPropertyB", true);
            m_client->SetSuperProperty(L"SuperPropertyC", 7.0);

            Assert::AreEqual(L"SuperValueA", m_client->GetSuperPropertyAsString(L"SuperPropertyA"), L"SuperPropertyA didn't match");
            Assert::IsTrue(m_client->GetSuperPropertyAsBool(L"SuperPropertyB"), L"SuperPropertyB didn't match");
            Assert::AreEqual(7.0, m_client->GetSuperPropertyAsDouble(L"SuperPropertyC"), L"SuperPropertyC didn't match");
        }

        TEST_METHOD(SuperPropertiesArePersistedAcrossClientInstances)
        {
            auto client = ref new MixpanelClient(DEFAULT_TOKEN);
            client->SetSuperProperty(L"SuperPropertyA", L"SuperValueA");

            client = ref new MixpanelClient(DEFAULT_TOKEN);
            auto superPropertyValue = client->GetSuperPropertyAsString(L"SuperPropertyA");
            Assert::AreEqual(L"SuperValueA", superPropertyValue, "Super Property wasn't persisted");

            // Since we don't want to rely on the destruction of the
            // super properties in the clear method, lets just clear the local state
            AsyncHelper::RunSynced(ApplicationData::Current->ClearAsync());

            Assert::IsTrue(0 == ApplicationData::Current->LocalSettings->Containers->Size, L"Expected local data to be empty");
        }

        TEST_METHOD(ClearingSuperPropertiesClearsAcrossInstances)
        {
            auto client = ref new MixpanelClient(DEFAULT_TOKEN);
            client->SetSuperProperty(L"SuperPropertyA", L"SuperValueA");

            client = ref new MixpanelClient(DEFAULT_TOKEN);
            auto superPropertyValue = client->GetSuperPropertyAsString(L"SuperPropertyA");
            Assert::AreEqual(L"SuperValueA", superPropertyValue, "Super Property wasn't persisted");

            client->ClearSuperProperties();

            client = ref new MixpanelClient(DEFAULT_TOKEN);
            Assert::IsFalse(client->HasSuperProperty(L"SuperPropertyA"), L"Didn't expect super property to be found");
        }

        TEST_METHOD(TimeOnlyAddedWhenAutomaticallyAttachingTimePropertyIsEnabled)
        {
            IPropertySet^ properties = ref new PropertySet();
            properties->Insert(L"StringValue", L"Value");
            m_client->AutomaticallyAttachTimeToEvents = false;

            auto trackPayload = m_client->GenerateTrackingJsonPayload(L"TestEvent", properties);
            auto propertiesPayload = trackPayload->GetNamedObject("properties");

            // Validate that the time property is not present (Since it was turned
            // off automatic attachment explicitly above)
            Assert::IsFalse(propertiesPayload->HasKey(L"time"), L"time key shouldn't be present");

            // Turn the automatic attachment of time back on
            m_client->AutomaticallyAttachTimeToEvents = true;

            trackPayload = m_client->GenerateTrackingJsonPayload(L"TestEvent", properties);
            propertiesPayload = trackPayload->GetNamedObject("properties");

            // Validate that the time is present, and is non-zero
            Assert::IsTrue(propertiesPayload->HasKey(L"time"), L"No time in properties payload");

            auto rawTimeValue = propertiesPayload->GetNamedValue("time");
            Assert::IsTrue(JsonValueType::Number == rawTimeValue->ValueType, L"Time was not the correct type");
            Assert::AreNotEqual(0.0, rawTimeValue->GetNumber()); //, L"time shouldn't have been 0");
        }

        TEST_METHOD(TimeDoesNotOverrideAnAlreadyExistingValueInThePropertiesPayload)
        {
            IPropertySet^ properties = ref new PropertySet();
            properties->Insert(L"StringValue", L"Value");
            properties->Insert(L"time", L"fakevalue");

            auto trackPayload = m_client->GenerateTrackingJsonPayload(L"TestEvent", properties);
            auto propertiesPayload = trackPayload->GetNamedObject("properties");

            // Validate that the time is present, and is the same as our original value
            Assert::IsTrue(propertiesPayload->HasKey(L"time"), L"No time in properties payload");

            auto rawTimeValue = propertiesPayload->GetNamedValue("time");
            Assert::IsFalse(JsonValueType::Number == rawTimeValue->ValueType, L"Time was not the correct type");
        }
#pragma endregion

#pragma region Asynchronous Queueing and Upload
        TEST_METHOD(QueuedEventsAreProcessedToStorage)
        {
            vector<shared_ptr<PayloadContainer>> written;

            m_client->SetWrittenToStorageMock([&written](auto wasWritten) {
                written.insert(begin(written), begin(wasWritten), end(wasWritten));
            });

			m_client->ConfigureForTesting(DEFAULT_IDLE_TIMEOUT, 1);

            m_client->Start();
            m_client->Track(L"TestEvent", nullptr);

            this_thread::sleep_for(DEFAULT_IDLE_TIMEOUT);

            Assert::AreEqual(1, (int)written.size(), L"Event wasn't written to disk");
        }

        TEST_METHOD(QueueCanBePaused)
        {
            m_client->Start();

            m_client->Track(L"TestEvent", nullptr);
            AsyncHelper::RunSynced(m_client->Pause());
        }

        TEST_METHOD(QueueCanBeCleared)
        {
			m_client->ForceWritingToStorage();
            m_client->Start();
            m_client->Track(L"TestEvent", nullptr);
            AsyncHelper::RunSynced(m_client->Pause());

            auto fileCount = AsyncHelper::RunSynced(create_task([]() -> task<int> {
                auto folder = co_await ApplicationData::Current->LocalFolder->GetFolderAsync(OVERRIDE_STORAGE_FOLDER);
                auto files = co_await folder->GetFilesAsync();

                return files->Size;
            }));

            Assert::AreEqual(1, fileCount, L"Wrong number of persisted items found");

            AsyncHelper::RunSynced(m_client->ClearStorageAsync());

            fileCount = AsyncHelper::RunSynced(create_task([]() -> task<int> {
                auto folder = co_await ApplicationData::Current->LocalFolder->GetFolderAsync(OVERRIDE_STORAGE_FOLDER);
                auto files = co_await folder->GetFilesAsync();

                return files->Size;
            }));

            Assert::AreEqual(0, fileCount, L"Didn't expect to find any items");
        }

		TEST_METHOD(RequestIndicatesFailureWhenCallingNonExistantEndPoint)
		{
			auto payload = ref new Map<String^, IJsonValue^>();
			auto wasSuccessful = MixpanelClient::SendRequestToService(
				ref new Uri(L"https://fake.codevoid.net"),
				payload,
				ref new HttpProductInfoHeaderValue(L"Codevoid.Mixpanel.MixpanelTests", L"1.0")).get();
			Assert::IsFalse(wasSuccessful);
		}

		TEST_METHOD(CanMakeRequestToPlaceholderService)
		{
			auto payload = ref new Map<String^, IJsonValue^>();
			payload->Insert(L"data", JsonObject::Parse("{ \"data\": 0 }"));
			auto wasSuccessful = MixpanelClient::SendRequestToService(
				ref new Uri(L"https://jsonplaceholder.typicode.com/posts"),
				payload,
				ref new HttpProductInfoHeaderValue(L"Codevoid.Mixpanel.MixpanelTests", L"1.0")).get();
			Assert::IsTrue(wasSuccessful);
		}

        TEST_METHOD(QueueIsUploaded)
        {
            vector<vector<IJsonValue^>> capturedPayloads;
            m_client->SetUploadToServiceMock([&capturedPayloads](auto, auto payloads, auto)
            {
                capturedPayloads.push_back(MixpanelTests::CaptureRequestPayloads(payloads));
                return task_from_result(true);
            });

			m_client->ConfigureForTesting(DEFAULT_IDLE_TIMEOUT, 1);

            m_client->Start();
            m_client->Track(L"TestEvent", nullptr);

            this_thread::sleep_for(DEFAULT_IDLE_TIMEOUT);

            Assert::AreEqual(1, (int)capturedPayloads.size(), L"Wrong number of payloads sent");
            Assert::AreEqual(1, (int)(capturedPayloads[0].size()), L"Wrong number of items in the first payload");
        }

        TEST_METHOD(BatchesIncludeMoreThanOneItem)
        {
            vector<vector<IJsonValue^>> capturedPayloads;
            m_client->SetUploadToServiceMock([&capturedPayloads](auto, auto payloads, auto)
            {
                capturedPayloads.push_back(MixpanelTests::CaptureRequestPayloads(payloads));
                return task_from_result(true);
            });
			m_client->ConfigureForTesting(DEFAULT_IDLE_TIMEOUT, 4);

            m_client->Track(L"TestEvent1", nullptr);
            m_client->Track(L"TestEvent2", nullptr);
            m_client->Track(L"TestEvent3", nullptr);
            m_client->Track(L"TestEvent4", nullptr);

            m_client->Start();

            this_thread::sleep_for(DEFAULT_IDLE_TIMEOUT);

            Assert::AreEqual(1, (int)capturedPayloads.size(), L"Wrong number of payloads sent");
            Assert::AreEqual(4, (int)(capturedPayloads[0].size()), L"Wrong number of items in the first payload");
        }

        TEST_METHOD(ItemsAreSpreadAcrossMultipleBatches)
        {
            vector<vector<IJsonValue^>> capturedPayloads;
            m_client->SetUploadToServiceMock([&capturedPayloads](auto, auto payloads, auto)
            {
                capturedPayloads.push_back(MixpanelTests::CaptureRequestPayloads(payloads));
                return task_from_result(true);
            });

            for (int i = 0; i < 150; i++)
            {
                m_client->Track(L"TrackEvent", nullptr);
            }

            m_client->Start();

            this_thread::sleep_for(DEFAULT_IDLE_TIMEOUT);

            Assert::AreEqual(3, (int)capturedPayloads.size(), L"Wrong number of payloads sent");
            Assert::AreEqual(50, (int)(capturedPayloads[0].size()), L"Wrong number of items in the first payload");
            Assert::AreEqual(50, (int)(capturedPayloads[1].size()), L"Wrong number of items in the second payload");
            Assert::AreEqual(50, (int)(capturedPayloads[2].size()), L"Wrong number of items in the third payload");
        }

        TEST_METHOD(ItemsAreRetriedIndividuallyAfterAFailure)
        {
			shared_ptr<vector<int>> capturedPayloadCounts = make_shared<vector<int>>();
            int itemsSeenCount = 0;
			int itemsSuccessfullyUploaded = 0;
            constexpr int FAILURE_TRIGGER = 75; // Fail in second batch

            m_client->SetUploadToServiceMock([capturedPayloadCounts, &itemsSeenCount, &itemsSuccessfullyUploaded, &FAILURE_TRIGGER](auto, auto payloads, auto)
            {
				int itemsInThisBatch = 0;

                for (auto item : MixpanelTests::CaptureRequestPayloads(payloads))
                {
                    itemsSeenCount++;
					itemsInThisBatch++;
                    if (itemsSeenCount == FAILURE_TRIGGER)
                    {
                        return task_from_result(false);
                    }
                }

				itemsSuccessfullyUploaded += itemsInThisBatch;
				capturedPayloadCounts->push_back(itemsInThisBatch);

                return task_from_result(true);
            });

            for (int i = 0; i < 100; i++)
            {
                m_client->Track(L"TrackEvent", nullptr);
            }

            m_client->Start();

			while (itemsSuccessfullyUploaded != 100)
			{
				this_thread::sleep_for(1ms);
			}

			Assert::AreEqual(51, (int)capturedPayloadCounts->size(), L"Wrong number of payloads sent");
			for (int i = 1; i < 50; i++)
			{
				Assert::AreEqual(1, (*capturedPayloadCounts)[i], L"Wrong number of items in single-item-payloads");
			}

            for (int i = 0; i < 50; i++)
            {
                m_client->Track(L"TrackEvent", nullptr);
            }

			while (itemsSuccessfullyUploaded != 150)
			{
				this_thread::sleep_for(1ms);
			}

            Assert::AreEqual(52, (int)capturedPayloadCounts->size(), L"Wrong number of payloads sent");
            Assert::AreEqual(50, (*capturedPayloadCounts)[51], L"Wrong number of items in the third payload");

			m_client->Shutdown().wait();
        }
#pragma endregion
    };
} } }
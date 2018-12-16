#include "pch.h"
#include "CppUnitTest.h"
#include "DurationTracker.h"
#include "PayloadEncoder.h"
#include "EngageConstants.h"
#include "MixpanelClient.h"
#include "AsyncHelper.h"

using namespace std;
using namespace std::chrono;
using namespace Platform;
using namespace Platform::Collections;
using namespace Codevoid::Tests::Utilities;
using namespace Codevoid::Utilities::Mixpanel;
using namespace concurrency;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Windows::Data::Json;
using namespace Windows::Foundation::Collections;
using namespace Windows::Storage;
using namespace Windows::Web::Http::Headers;

constexpr auto DEFAULT_TOKEN = L"DEFAULT_TOKEN";
constexpr auto OVERRIDE_STORAGE_FOLDER = L"MixpanelClientTests";
constexpr auto OVERRIDE_PROFILE_STORAGE_FOLDER = L"MixpanelClientTests\\Profile";
constexpr milliseconds DEFAULT_IDLE_TIMEOUT = 10ms;
constexpr size_t SPIN_LOOP_LIMIT = 1000;
constexpr auto DISTINCT_ENGAGE_KEY = L"$distinct_id";
constexpr auto TOKEN_ENGAGE_KEY = L"$token";

extern std::optional<steady_clock::time_point> g_overrideNextTimeAccess;

void SetNextClockAccessTime_MixpanelClient(const steady_clock::time_point& advanceTo)
{
    g_overrideNextTimeAccess = advanceTo;
}

void SpinWaitForItemCount(const int& count, const int target)
{
    size_t loopCount = 0;
    while ((count < target) && (loopCount < SPIN_LOOP_LIMIT))
    {
        loopCount++;
        this_thread::sleep_for(2ms);
    }

    Assert::IsTrue(count >= target, L"Spin Wait looped too long and never reached target");
}

IPropertySet^ GetPropertySetWithStuffInIt()
{
    auto properties = ref new PropertySet();
    properties->Insert(L"Key", L"Value");

    return properties;
}

namespace Codevoid::Tests::Mixpanel
{
    TEST_CLASS(MixpanelTests)
    {
    private:
        MixpanelClient^ m_client;

        static task<StorageFolder^> GetAndClearTestFolder(String^ folder)
        {
            auto storageFolder = co_await ApplicationData::Current->LocalFolder->CreateFolderAsync(
                folder,
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

        static task<int> WriteTestPayload(String^ folderName)
        {
            auto storageFolder = co_await ApplicationData::Current->LocalFolder->CreateFolderAsync(folderName,
                CreationCollisionOption::OpenIfExists);

            JsonObject^ payload = ref new JsonObject();

            auto title = JsonValue::CreateStringValue(L"SampleTitle");
            payload->Insert(L"title", title);

            int itemsWritten = 1;
            for (; itemsWritten <= 3; itemsWritten++)
            {
                auto fileName = ref new String(std::to_wstring(itemsWritten).append(L".json").c_str());
                auto file = co_await storageFolder->CreateFileAsync(fileName, CreationCollisionOption::ReplaceExisting);
                co_await FileIO::WriteTextAsync(file, payload->Stringify());
            }

            return itemsWritten;
        }

    public:
        TEST_METHOD_INITIALIZE(InitializeClass)
        {
            m_client = ref new MixpanelClient(StringReference(DEFAULT_TOKEN));

            // Disable Persistence of Super properties
            // to help maintain these tests as stateless
            m_client->PersistSuperPropertiesToApplicationData = false;

            // We don't want the automatic session tracking
            // to add additional events when we're not expecting it.
            m_client->AutomaticallyTrackSessions = false;

            auto trackFolder = AsyncHelper::RunSynced(GetAndClearTestFolder(StringReference(OVERRIDE_STORAGE_FOLDER)));
            auto profileFolder = AsyncHelper::RunSynced(GetAndClearTestFolder(StringReference(OVERRIDE_PROFILE_STORAGE_FOLDER)));

            // The URL here is a helpful endpoint on the internet that just round-files
            // the requests to enable simple testing.
            m_client->Initialize(trackFolder, profileFolder, ref new Uri(L"https://jsonplaceholder.typicode.com/posts"));
            m_client->ConfigureForTesting(DEFAULT_IDLE_TIMEOUT, 10);

            // Set the default service mock to avoid sending things to the
            // internet when we don't really need to.
            m_client->SetUploadToServiceMock([](auto uri, auto payload, auto)
            {
                return task_from_result(true);
            });
        }

        TEST_METHOD(InitializeAsyncRestoresQueuedToStorageItems)
        {
            // Since this test doesn't use the one created
            // in test init, lets shut it down
            m_client->Shutdown().wait();
            m_client = nullptr;

            AsyncHelper::RunSynced(WriteTestPayload(L"MixpanelUploadQueue"));
            AsyncHelper::RunSynced(WriteTestPayload(L"MixpanelUploadQueue\\Profile"));
            m_client = ref new MixpanelClient(StringReference(DEFAULT_TOKEN));
            m_client->PersistSuperPropertiesToApplicationData = false;
            
            AsyncHelper::RunSynced(m_client->InitializeAsync());

            vector<vector<IJsonValue^>> trackPayloads;
            vector<vector<IJsonValue^>> profilePayloads;

            atomic<int> itemCounts = 0;
            m_client->SetUploadToServiceMock([&trackPayloads, &itemCounts, &profilePayloads](Uri^ uri, auto payloads, auto)
            {
                auto convertedPayloads = MixpanelTests::CaptureRequestPayloads(payloads);
                if (uri->Path == StringReference(L"/track"))
                {
                    trackPayloads.push_back(convertedPayloads);
                }

                if (uri->Path == StringReference(L"/engage"))
                {
                    profilePayloads.push_back(convertedPayloads);
                }

                itemCounts += (int)convertedPayloads.size();
                return task_from_result(true);
            });

            m_client->ConfigureForTesting(DEFAULT_IDLE_TIMEOUT, 1);
            m_client->Start();

            SpinWaitForItemCount(itemCounts, 6);

            Assert::AreEqual(6, itemCounts.load(), L"Persisted Items weren't supplied to upload correctly");

            AsyncHelper::RunSynced(m_client->ClearStorageAsync());
            m_client->Shutdown().wait();
            m_client = nullptr;
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
            auto client = ref new MixpanelClient(StringReference(DEFAULT_TOKEN));

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

        TEST_METHOD(UpdateProfileThrowsIfNotInitialized)
        {
            bool exceptionThrown = false;
            auto client = ref new MixpanelClient(StringReference(DEFAULT_TOKEN));

            try
            {
                client->UpdateProfile(UserProfileOperation::Set, ref new ValueSet());
            }
            catch (InvalidArgumentException^ ex)
            {
                exceptionThrown = true;
            }

            Assert::IsTrue(exceptionThrown, L"Didn't get expected exception");
        }

        TEST_METHOD(UpdateProfileThrowsWhenNoPropertiesProvided)
        {
            bool exceptionThrown = false;
            auto client = ref new MixpanelClient(StringReference(DEFAULT_TOKEN));

            try
            {
                client->UpdateProfile(UserProfileOperation::Set, nullptr);
            }
            catch (InvalidArgumentException^ ex)
            {
                exceptionThrown = true;
            }

            Assert::IsTrue(exceptionThrown, L"Didn't get expected exception");
        }

        TEST_METHOD(UpdateProfileThrowsWhenEmptyPropertiesProvided)
        {
            bool exceptionThrown = false;
            auto client = ref new MixpanelClient(StringReference(DEFAULT_TOKEN));

            try
            {
                client->UpdateProfile(UserProfileOperation::Set, ref new ValueSet());
            }
            catch (InvalidArgumentException^ ex)
            {
                exceptionThrown = true;
            }

            Assert::IsTrue(exceptionThrown, L"Didn't get expected exception");
        }

        TEST_METHOD(GeneratingJsonObjectDoesntThrowForSupportedTypes)
        {
            IVector<String^>^ stringVector = ref new Vector<String^>();
            stringVector->Append(ref new String(L"1"));
            IVector<int>^ intVector = ref new Vector<int>({ 1, 2, 3 });
            IVector<float>^ floatVector = ref new Vector<float>({ 1.0f, 2.0f, 3.0f });
            IVector<double>^ doubleVector = ref new Vector<double>({ 1.0, 2.0, 3.0 });
            IPropertySet^ properties = ref new PropertySet();
            properties->Insert(L"StringValue", L"Value");
            properties->Insert(L"IntValue", 42);
            properties->Insert(L"DoubleValue", 4.0);
            properties->Insert(L"FloatValue", 4.0f);
            properties->Insert(L"BooleanValue", true);
            auto calendar = ref new Windows::Globalization::Calendar();
            properties->Insert(L"DateTimeValue", calendar->GetDateTime());
            properties->Insert(L"StringVector", stringVector);
            properties->Insert(L"IntegerVector", intVector);
            //properties->Insert(L"FloatVector", floatVector);
            //properties->Insert(L"DoubleVector", doubleVector);

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
            IVector<String^>^ stringVector = ref new Vector<String^>();
            stringVector->Append(ref new String(L"1"));
            IVector<int>^ intVector = ref new Vector<int>({ 1, 2, 3 });
            IVector<float>^ floatVector = ref new Vector<float>({ 1.0f, 2.0f, 3.0f });
            IVector<double>^ doubleVector = ref new Vector<double>({ 1.0, 2.0, 3.0 });

            IPropertySet^ properties = ref new PropertySet();
            properties->Insert(L"StringValue", L"Value");
            properties->Insert(L"IntValue", 42);
            properties->Insert(L"DoubleValue", 4.1);
            properties->Insert(L"FloatValue", 4.2f);
            properties->Insert(L"BooleanValue", true);
            auto calendar = ref new Windows::Globalization::Calendar();
            auto insertedDateTime = calendar->GetDateTime();
            properties->Insert(L"DateTimeValue", insertedDateTime);
            properties->Insert(L"StringVector", stringVector);
            properties->Insert(L"IntegerVector", intVector);
            properties->Insert(L"FloatVector", floatVector);
            properties->Insert(L"DoubleVector", doubleVector);

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
            Assert::IsTrue(result->HasKey(L"DateTimeValue"), L"DateTime key isn't present");
            String^ dateTime = result->GetNamedString(L"DateTimeValue");
            Assert::AreEqual(DateTimeToMixpanelDateFormat(insertedDateTime), dateTime, L"DateTimeValue didn't match");

            Assert::IsTrue(result->HasKey(L"StringVector"), L"String Vector isn't present");
            JsonArray^ stringArray = result->GetNamedArray(L"StringVector");
            Assert::AreEqual(1, (int)stringArray->Size, L"Wrong Number of items in string vector");
            Assert::AreEqual(L"1", stringArray->GetStringAt(0), L"Wrong value in string vector");

            Assert::IsTrue(result->HasKey(L"IntegerVector"), L"Integer Vector isn't present");
            JsonArray^ integerArray = result->GetNamedArray(L"IntegerVector");
            Assert::AreEqual(3, (int)integerArray->Size, L"Wrong Number of items in string vector");
            Assert::AreEqual((double)1, integerArray->GetNumberAt(0), L"Wrong value in integer vector");
            Assert::AreEqual((double)2, integerArray->GetNumberAt(1), L"Wrong value in integer vector");
            Assert::AreEqual((double)3, integerArray->GetNumberAt(2), L"Wrong value in integer vector");

            Assert::IsTrue(result->HasKey(L"FloatVector"), L"Float Vector isn't present");
            JsonArray^ floatArray = result->GetNamedArray(L"FloatVector");
            Assert::AreEqual(3, (int)floatArray->Size, L"Wrong Number of items in float vector");
            Assert::AreEqual((double)1.0f, floatArray->GetNumberAt(0), L"Wrong value in float vector");
            Assert::AreEqual((double)2.0f, floatArray->GetNumberAt(1), L"Wrong value in float vector");
            Assert::AreEqual((double)3.0f, floatArray->GetNumberAt(2), L"Wrong value in float vector");

            Assert::IsTrue(result->HasKey(L"DoubleVector"), L"Double Vector isn't present");
            JsonArray^ doubleArray = result->GetNamedArray(L"DoubleVector");
            Assert::AreEqual(3, (int)doubleArray->Size, L"Wrong Number of items in Double vector");
            Assert::AreEqual((double)1.0f, doubleArray->GetNumberAt(0), L"Wrong value in Double vector");
            Assert::AreEqual((double)2.0f, doubleArray->GetNumberAt(1), L"Wrong value in Double vector");
            Assert::AreEqual((double)3.0f, doubleArray->GetNumberAt(2), L"Wrong value in Double vector");
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

        TEST_METHOD(CanEncodeNumericValuesInJson)
        {
            IPropertySet^ properties = ref new PropertySet();
            properties->Insert(L"IntValue", 42);
            properties->Insert(L"DoubleValue", 4.1);
            properties->Insert(L"FloatValue", 4.2f);

            auto result = ref new JsonObject();
            MixpanelClient::AppendNumericPropertySetToJsonPayload(properties, result);

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
        }

        TEST_METHOD(ExceptionThrownWhenIncludingNonNumericValuesInPropertySet)
        {
            IPropertySet^ properties = ref new PropertySet();
            properties->Insert(L"Bar", 3.14);
            properties->Insert(L"Foo", L"Value");

            bool exceptionThrown = false;
            JsonObject^ result = ref new JsonObject();

            try
            {
                MixpanelClient::AppendNumericPropertySetToJsonPayload(properties, result);
            }
            catch (InvalidCastException^ ex)
            {
                exceptionThrown = true;
            }

            Assert::IsTrue(exceptionThrown, L"Didn't get exception for non-numeric property in property set");
        }

        TEST_METHOD(TrackingPayloadIncludesTokenAndPayload)
        {
            IPropertySet^ properties = ref new PropertySet();
            properties->Insert(L"StringValue", L"Value");

            properties = m_client->EmbelishPropertySetForTrack(properties);
            auto trackPayload = MixpanelClient::GenerateTrackJsonPayload(L"TestEvent", properties);

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
            Assert::AreEqual(StringReference(DEFAULT_TOKEN), propertiesPayload->GetNamedString(L"token"), L"Token had incorrect value");
        }

        TEST_METHOD(TrackingPayloadIncludesTokenAndSuperPropertiesPayload)
        {
            IPropertySet^ properties = ref new PropertySet();
            properties->Insert(L"StringValue", L"Value");
            m_client->SetSuperPropertyAsString(L"SuperPropertyA", L"SuperValueA");
            m_client->SetSuperPropertyAsDouble(L"SuperPropertyB", 7.0);
            m_client->SetSuperPropertyAsBoolean(L"SuperPropertyC", true);
            m_client->SetSuperPropertyAsInteger(L"SuperPropertyD", 1);

            properties = m_client->EmbelishPropertySetForTrack(properties);
            auto trackPayload = MixpanelClient::GenerateTrackJsonPayload(L"TestEvent", properties);

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
            Assert::AreEqual(StringReference(DEFAULT_TOKEN), propertiesPayload->GetNamedString(L"token"), L"Token had incorrect value");

            // Validate that Super Property A is present
            Assert::IsTrue(propertiesPayload->HasKey(L"SuperPropertyA"), L"No SuperPropertyA in properties payload");
            Assert::AreEqual(L"SuperValueA", propertiesPayload->GetNamedString(L"SuperPropertyA"), L"SuperPropertyA had incorrect value");

            // Validate that Super Property B is present
            Assert::IsTrue(propertiesPayload->HasKey(L"SuperPropertyB"), L"No SuperPropertyB in properties payload");
            Assert::AreEqual(7.0, propertiesPayload->GetNamedNumber(L"SuperPropertyB"), L"SuperPropertyB had incorrect value");

            // Validate that Super Property C is present
            Assert::IsTrue(propertiesPayload->HasKey(L"SuperPropertyC"), L"No SuperPropertyC in properties payload");
            Assert::AreEqual(true, propertiesPayload->GetNamedBoolean(L"SuperPropertyC"), L"SuperPropertyC had incorrect value");

            // Validate that Super Property D is present
            Assert::IsTrue(propertiesPayload->HasKey(L"SuperPropertyD"), L"No SuperPropertyD in properties payload");
            Assert::AreEqual(1.0, propertiesPayload->GetNamedNumber(L"SuperPropertyD"), L"SuperPropertyD had incorrect value");
        }

        TEST_METHOD(CanSetSuperPropertyMoreThanOnce)
        {
            m_client->SetSuperPropertyAsString(L"SuperPropertyA", L"SuperValueA");

            IPropertySet^ properties = m_client->EmbelishPropertySetForTrack(nullptr);
            auto trackPayload = MixpanelClient::GenerateTrackJsonPayload(L"TestEvent", properties);

            // Check that the actual properties we passed in are present
            Assert::IsTrue(trackPayload->HasKey(L"properties"), L"No properties payload");
            auto propertiesPayload = trackPayload->GetNamedObject("properties");

            // Validate that Super Property is present
            Assert::IsTrue(propertiesPayload->HasKey(L"SuperPropertyA"), L"No SuperPropertyA in properties payload");
            Assert::AreEqual(L"SuperValueA", propertiesPayload->GetNamedString(L"SuperPropertyA"), L"SuperPropertyA had incorrect value");

            // Set the super property a second time
            m_client->SetSuperPropertyAsString(L"SuperPropertyA", L"DifferentValue");

            // Validate payload again
            properties = m_client->EmbelishPropertySetForTrack(nullptr);
            trackPayload = MixpanelClient::GenerateTrackJsonPayload(L"TestEvent", properties);
            propertiesPayload = trackPayload->GetNamedObject("properties");

            // Validate that Super Property is present
            Assert::IsTrue(propertiesPayload->HasKey(L"SuperPropertyA"), L"No SuperPropertyA in properties payload");
            Assert::AreEqual(L"DifferentValue", propertiesPayload->GetNamedString(L"SuperPropertyA"), L"SuperPropertyA had incorrect value");
        }

        TEST_METHOD(CanCheckForSuperPropertyWhenNotSet)
        {
            Assert::IsFalse(m_client->HasSuperProperty(L"SuperPropertyA"), L"SuperPropertyA shouldn't have been in the list");
        }

        TEST_METHOD(CanCheckForSuperPropertyWhenSet)
        {
            m_client->SetSuperPropertyAsString(L"SuperPropertyA", L"SuperValueA");
            Assert::IsTrue(m_client->HasSuperProperty(L"SuperPropertyA"), L"SuperPropertyA not in list");
        }

        TEST_METHOD(CanReadBackSuperProperties)
        {
            m_client->SetSuperPropertyAsString(L"SuperPropertyA", L"SuperValueA");
            m_client->SetSuperPropertyAsBoolean(L"SuperPropertyB", true);
            m_client->SetSuperPropertyAsDouble(L"SuperPropertyC", 7.0);

            Assert::AreEqual(L"SuperValueA", m_client->GetSuperPropertyAsString(L"SuperPropertyA"), L"SuperPropertyA didn't match");
            Assert::IsTrue(m_client->GetSuperPropertyAsBool(L"SuperPropertyB"), L"SuperPropertyB didn't match");
            Assert::AreEqual(7.0, m_client->GetSuperPropertyAsDouble(L"SuperPropertyC"), L"SuperPropertyC didn't match");
        }

        TEST_METHOD(CanRemoveSuperProperty)
        {
            constexpr auto PROPERTY_NAME = L"SuperProperty";

            // Add Property, and validate it actualy makes it before we
            // try to remove it
            m_client->SetSuperPropertyAsString(StringReference(PROPERTY_NAME), L"SuperValueA");
            Assert::IsTrue(m_client->HasSuperProperty(StringReference(PROPERTY_NAME)), L"Property wasn't found; expected it");
            
            m_client->RemoveSuperProperty(StringReference(PROPERTY_NAME));
            Assert::IsFalse(m_client->HasSuperProperty(StringReference(PROPERTY_NAME)), L"Proprety found; shouldn't have been present");
        }
        
        TEST_METHOD(SuperPropertiesArePersistedAcrossClientInstances)
        {
            auto client = ref new MixpanelClient(StringReference(DEFAULT_TOKEN));
            client->SetSuperPropertyAsString(L"SuperPropertyA", L"SuperValueA");

            client = ref new MixpanelClient(StringReference(DEFAULT_TOKEN));
            auto superPropertyValue = client->GetSuperPropertyAsString(L"SuperPropertyA");
            Assert::AreEqual(L"SuperValueA", superPropertyValue, "Super Property wasn't persisted");

            // Since we don't want to rely on the destruction of the
            // super properties in the clear method, lets just clear the local state
            AsyncHelper::RunSynced(ApplicationData::Current->ClearAsync());

            Assert::IsTrue(0 == ApplicationData::Current->LocalSettings->Containers->Size, L"Expected local data to be empty");
        }

        TEST_METHOD(CanClearSuperProperties)
        {
            m_client->SetSuperPropertyAsString(L"SuperPropertyA", L"SuperValueA");

            // Validate that Super Property is present
            Assert::IsTrue(m_client->HasSuperProperty(L"SuperPropertyA"), L"No SuperPropertyA in properties payload");
            Assert::AreEqual(L"SuperValueA", m_client->GetSuperPropertyAsString(L"SuperPropertyA"), L"SuperPropertyA had incorrect value");

            // Clear the super properties, and generate the payload again
            m_client->ClearSuperProperties();

            // Validate that Super Property isn't present
            Assert::IsFalse(m_client->HasSuperProperty(L"SuperPropertyA"), L"SuperPropertyA present, when it should have been cleared");
        }

        TEST_METHOD(ClearingSuperPropertiesClearsAcrossInstances)
        {
            auto client = ref new MixpanelClient(StringReference(DEFAULT_TOKEN));
            client->SetSuperPropertyAsString(L"SuperPropertyA", L"SuperValueA");

            client = ref new MixpanelClient(StringReference(DEFAULT_TOKEN));
            auto superPropertyValue = client->GetSuperPropertyAsString(L"SuperPropertyA");
            Assert::AreEqual(L"SuperValueA", superPropertyValue, "Super Property wasn't persisted");

            client->ClearSuperProperties();

            client = ref new MixpanelClient(StringReference(DEFAULT_TOKEN));
            Assert::IsFalse(client->HasSuperProperty(L"SuperPropertyA"), L"Didn't expect super property to be found");
        }

        TEST_METHOD(SettingPropertyInPayloadOverridesSuperProperty)
        {
            constexpr auto SUPER_PROPERTY_VALUE = 7;
            constexpr auto LOCAL_PROPERTY_VALUE = 8;
            const auto propertyName = StringReference(L"SuperProperty");
            m_client->SetSuperPropertyAsInteger(propertyName, SUPER_PROPERTY_VALUE);

            IPropertySet^ properties = ref new PropertySet();
            properties->Insert(propertyName, LOCAL_PROPERTY_VALUE);
            properties = m_client->EmbelishPropertySetForTrack(properties);
            
            auto retrievedValue = static_cast<int>(properties->Lookup(propertyName));
            Assert::AreEqual(LOCAL_PROPERTY_VALUE, retrievedValue, L"Property set didn't allow local value to override super properties");
        }

        TEST_METHOD(SuperPropertiesAttachedOnNewInstanceWithoutSettingSuperProperty)
        {
            const auto PROPERTY_NAME = StringReference(L"SuperPropertyK");
            const auto PROPERTY_VALUE = StringReference(L"PropertyValueK");
            auto client = ref new MixpanelClient(StringReference(DEFAULT_TOKEN));
            client->SetSuperPropertyAsString(PROPERTY_NAME, PROPERTY_VALUE);

            client = ref new MixpanelClient(StringReference(DEFAULT_TOKEN));
            auto properties = client->EmbelishPropertySetForTrack(nullptr);
            auto trackPayload = MixpanelClient::GenerateTrackJsonPayload(L"TestEvent", properties);

            auto propertiesPayload = trackPayload->GetNamedObject("properties");
            Assert::IsTrue(propertiesPayload->HasKey(PROPERTY_NAME), L"Super Property not found");
            Assert::AreEqual(PROPERTY_VALUE, propertiesPayload->GetNamedString(PROPERTY_NAME), L"Wrong value in the payload");
        }

        TEST_METHOD(TimeOnlyAddedWhenAutomaticallyAttachingTimePropertyIsEnabled)
        {
            IPropertySet^ properties = ref new PropertySet();
            properties->Insert(L"StringValue", L"Value");
            m_client->AutomaticallyAttachTimeToEvents = false;

            properties = m_client->EmbelishPropertySetForTrack(properties);
            auto trackPayload = MixpanelClient::GenerateTrackJsonPayload(L"TestEvent", properties);
            auto propertiesPayload = trackPayload->GetNamedObject("properties");

            // Validate that the time property is not present (Since it was turned
            // off automatic attachment explicitly above)
            Assert::IsFalse(propertiesPayload->HasKey(L"time"), L"time key shouldn't be present");

            // Turn the automatic attachment of time back on
            m_client->AutomaticallyAttachTimeToEvents = true;

            properties = m_client->EmbelishPropertySetForTrack(properties);
            trackPayload = MixpanelClient::GenerateTrackJsonPayload(L"TestEvent", properties);
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

            properties = m_client->EmbelishPropertySetForTrack(properties);
            auto trackPayload = MixpanelClient::GenerateTrackJsonPayload(L"TestEvent", properties);
            auto propertiesPayload = trackPayload->GetNamedObject("properties");

            // Validate that the time is present, and is the same as our original value
            Assert::IsTrue(propertiesPayload->HasKey(L"time"), L"No time in properties payload");

            auto rawTimeValue = propertiesPayload->GetNamedValue("time");
            Assert::IsFalse(JsonValueType::Number == rawTimeValue->ValueType, L"Time was not the correct type");
        }

        TEST_METHOD(CanSetGetAndCheckSessionProperty)
        {
            m_client->SetSessionPropertyAsBoolean(L"SessionPropertyA", true);
            m_client->SetSessionPropertyAsInteger(L"SessionPropertyB", 1);
            m_client->SetSessionPropertyAsDouble(L"SessionPropertyC", 1.0);
            m_client->SetSessionPropertyAsString(L"SessionPropertyD", L"true");

            Assert::IsFalse(m_client->HasSessionProperty("SessionPropertyMissing"), L"Didn't expect to find non-existant property");

            Assert::IsTrue(m_client->HasSessionProperty(L"SessionPropertyA"), L"SessionPropertyA not set");
            Assert::AreEqual(true, m_client->GetSessionPropertyAsBool(L"SessionPropertyA"), L"SessionPropertyA had wrong value");

            Assert::IsTrue(m_client->HasSessionProperty(L"SessionPropertyB"), L"SessionPropertyB not set");
            Assert::AreEqual(1, m_client->GetSessionPropertyAsInteger(L"SessionPropertyB"), L"SessionPropertyB had wrong value");

            Assert::IsTrue(m_client->HasSessionProperty(L"SessionPropertyC"), L"SessionPropertyC not set");
            Assert::AreEqual(1.0, m_client->GetSessionPropertyAsDouble(L"SessionPropertyC"), L"SessionPropertyC had wrong value");

            Assert::IsTrue(m_client->HasSessionProperty(L"SessionPropertyD"), L"SessionPropertyD not set");
            Assert::AreEqual(L"true", m_client->GetSessionPropertyAsString(L"SessionPropertyD"), L"SessionPropertyD had wrong value");
        }

        TEST_METHOD(CanRemoveSessionProperty)
        {
            m_client->SetSessionPropertyAsBoolean(L"SessionPropertyA", true);

            Assert::IsTrue(m_client->HasSessionProperty(L"SessionPropertyA"), L"SessionPropertyA not set");

            m_client->RemoveSessionProperty(L"SessionPropertyA");

            Assert::IsFalse(m_client->HasSessionProperty(L"SessionPropertyA"), L"SessionPropertyA should have been removed set");
        }

        TEST_METHOD(CanClearSessionProperties)
        {
            m_client->SetSessionPropertyAsBoolean(L"SessionPropertyA", true);
            m_client->SetSessionPropertyAsBoolean(L"SessionPropertyB", true);

            Assert::IsTrue(m_client->HasSessionProperty(L"SessionPropertyA"), L"SessionPropertyA not set");
            Assert::IsTrue(m_client->HasSessionProperty(L"SessionPropertyB"), L"SessionPropertyB not set");

            m_client->ClearSessionProperties();

            Assert::IsFalse(m_client->HasSessionProperty(L"SessionPropertyA"), L"SessionPropertyA should have been removed set");
            Assert::IsFalse(m_client->HasSessionProperty(L"SessionPropertyB"), L"SessionPropertyB should have been removed set");
        }
#pragma endregion

#pragma region Asynchronous Queueing and Upload
        TEST_METHOD(QueuedEventsAreProcessedToStorage)
        {
            vector<shared_ptr<PayloadContainer>> trackWritten;
            vector<shared_ptr<PayloadContainer>> profileWritten;

            m_client->SetTrackWrittenToStorageMock([&trackWritten](auto wasWritten) {
                trackWritten.insert(begin(trackWritten), begin(wasWritten), end(wasWritten));
            });

            m_client->SetProfileWrittenToStorageMock([&profileWritten](auto wasWritten) {
                profileWritten.insert(begin(profileWritten), begin(wasWritten), end(wasWritten));
            });

            m_client->ConfigureForTesting(DEFAULT_IDLE_TIMEOUT, 1);
            m_client->GenerateAndSetUserIdentity();

            m_client->Start();
            m_client->Track(L"TestEvent", nullptr);
            m_client->UpdateProfile(UserProfileOperation::Set, GetPropertySetWithStuffInIt());
            this_thread::sleep_for(DEFAULT_IDLE_TIMEOUT);

            Assert::AreEqual(1, (int)trackWritten.size(), L"Event wasn't written to disk");
            Assert::AreEqual(1, (int)profileWritten.size(), L"Profile update wasn't written to disk");
        }

        TEST_METHOD(EventsAreNotProcessedWhenDropEventsForPrivacyIsEnabled)
        {
            m_client->DropEventsForPrivacy = true;

            vector<shared_ptr<PayloadContainer>> written;

            m_client->SetTrackWrittenToStorageMock([&written](auto wasWritten) {
                written.insert(begin(written), begin(wasWritten), end(wasWritten));
            });

            m_client->ConfigureForTesting(DEFAULT_IDLE_TIMEOUT, 1);

            m_client->Start();
            m_client->Track(L"TestEvent", nullptr);

            this_thread::sleep_for(DEFAULT_IDLE_TIMEOUT);
            AsyncHelper::RunSynced(m_client->Shutdown());

            Assert::AreEqual(0, (int)written.size(), L"Event event shouldn't have been written to disk");
        }

        TEST_METHOD(ProfileUpdatesAreDroppedWhenPrivacyIsEnabled)
        {
            m_client->DropEventsForPrivacy = true;

            vector<shared_ptr<PayloadContainer>> written;

            m_client->SetTrackWrittenToStorageMock([&written](auto wasWritten) {
                written.insert(begin(written), begin(wasWritten), end(wasWritten));
            });

            m_client->ConfigureForTesting(DEFAULT_IDLE_TIMEOUT, 1);

            auto properties = ref new PropertySet();
            properties->Insert(L"Property", L"Value");

            m_client->Start();
            m_client->UpdateProfile(UserProfileOperation::Set, properties);

            this_thread::sleep_for(DEFAULT_IDLE_TIMEOUT);
            AsyncHelper::RunSynced(m_client->Shutdown());

            Assert::AreEqual(0, (int)written.size(), L"Event event shouldn't have been written to disk");
        }

        TEST_METHOD(EventsAlreadyInStorageAreIgnoredWhenDroppingForPrivacyEnabledBeforeStartup)
        {
            // Since this test doesn't use the one created
            // in test init, lets shut it down
            m_client->Shutdown().wait();
            m_client = nullptr;

            AsyncHelper::RunSynced(WriteTestPayload(L"MixpanelUploadQueue"));
            AsyncHelper::RunSynced(WriteTestPayload(L"MixpanelUploadQueue\\Profile"));
            m_client = ref new MixpanelClient(StringReference(DEFAULT_TOKEN));
            m_client->DropEventsForPrivacy = true;
            m_client->PersistSuperPropertiesToApplicationData = false;

            AsyncHelper::RunSynced(m_client->InitializeAsync());

            atomic<int> itemCounts = 0;
            m_client->SetUploadToServiceMock([&itemCounts](Uri^ uri, auto payloads, auto)
            {
                itemCounts += 1;
                return task_from_result(true);
            });

            m_client->ConfigureForTesting(DEFAULT_IDLE_TIMEOUT, 1);
            m_client->Start();

            AsyncHelper::RunSynced(m_client->ClearStorageAsync());
            m_client->Shutdown().wait();
            m_client = nullptr;

            Assert::AreEqual(0, itemCounts.load(), L"Items Shouldn't have been uploaded");
        }

        TEST_METHOD(QueueCanBePaused)
        {
            m_client->Start();

            m_client->Track(L"TestEvent", nullptr);
            AsyncHelper::RunSynced(m_client->PauseAsync());
        }

        TEST_METHOD(QueueCanBeCleared)
        {
            m_client->GenerateAndSetUserIdentity();
            m_client->ForceWritingToStorage();
            m_client->Start();
            m_client->Track(L"TestEvent", nullptr);
            m_client->UpdateProfile(UserProfileOperation::Set, GetPropertySetWithStuffInIt());

            AsyncHelper::RunSynced(m_client->PauseAsync());

            auto trackFileCount = AsyncHelper::RunSynced(create_task([]() -> task<int> {
                auto folder = co_await ApplicationData::Current->LocalFolder->GetFolderAsync(StringReference(OVERRIDE_STORAGE_FOLDER));
                auto files = co_await folder->GetFilesAsync();

                return files->Size;
            }));

            auto profileFileCount = AsyncHelper::RunSynced(create_task([]() -> task<int> {
                auto folder = co_await ApplicationData::Current->LocalFolder->GetFolderAsync(StringReference(OVERRIDE_PROFILE_STORAGE_FOLDER));
                auto files = co_await folder->GetFilesAsync();

                return files->Size;
            }));

            Assert::AreEqual(1, trackFileCount, L"Wrong number of track persisted items found");
            Assert::AreEqual(1, profileFileCount, L"Wrong number of profile persisted items found");

            AsyncHelper::RunSynced(m_client->ClearStorageAsync());

            trackFileCount = AsyncHelper::RunSynced(create_task([]() -> task<int> {
                auto folder = co_await ApplicationData::Current->LocalFolder->GetFolderAsync(StringReference(OVERRIDE_STORAGE_FOLDER));
                auto files = co_await folder->GetFilesAsync();

                return files->Size;
            }));

            Assert::AreEqual(0, trackFileCount, L"Didn't expect to find any items");

            profileFileCount = AsyncHelper::RunSynced(create_task([]() -> task<int> {
                auto folder = co_await ApplicationData::Current->LocalFolder->GetFolderAsync(StringReference(OVERRIDE_PROFILE_STORAGE_FOLDER));
                auto files = co_await folder->GetFilesAsync();

                return files->Size;
            }));

            Assert::AreEqual(0, profileFileCount, L"Didn't expect to find any items");
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
            vector<vector<IJsonValue^>> trackPayloads;
            vector<vector<IJsonValue^>> profilePayloads;
            m_client->SetUploadToServiceMock([&trackPayloads, &profilePayloads](Uri^ uri, auto payloads, auto)
            {
                auto capturedPayload = MixpanelTests::CaptureRequestPayloads(payloads);
                if (uri->Path == StringReference(L"/track"))
                {
                    trackPayloads.push_back(capturedPayload);
                }

                if (uri->Path == StringReference(L"/engage"))
                {
                    profilePayloads.push_back(capturedPayload);
                }

                return task_from_result(true);
            });

            m_client->ConfigureForTesting(DEFAULT_IDLE_TIMEOUT, 1);
            m_client->GenerateAndSetUserIdentity();
            m_client->Start();
            m_client->Track(L"TestEvent", nullptr);
            m_client->UpdateProfile(UserProfileOperation::Set, GetPropertySetWithStuffInIt());

            this_thread::sleep_for(DEFAULT_IDLE_TIMEOUT * 100);

            Assert::AreEqual(1, (int)trackPayloads.size(), L"Wrong number of track payloads sent");
            Assert::AreEqual(1, (int)(trackPayloads[0].size()), L"Wrong number of items in the first track payload");
            Assert::AreEqual(1, (int)profilePayloads.size(), L"Wrong number of profile payloads sent");
            Assert::AreEqual(1, (int)(profilePayloads[0].size()), L"Wrong number of items in the first profile payload");
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

            this_thread::sleep_for(DEFAULT_IDLE_TIMEOUT * 10);

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

                capturedPayloadCounts->push_back(itemsInThisBatch);
                itemsSuccessfullyUploaded += itemsInThisBatch;

                return task_from_result(true);
            });

            for (int i = 0; i < 100; i++)
            {
                m_client->Track(L"TrackEvent", nullptr);
            }

            m_client->Start();

            SpinWaitForItemCount(itemsSuccessfullyUploaded, 100);

            Assert::AreEqual(51, (int)capturedPayloadCounts->size(), L"Wrong number of payloads sent");
            for (int i = 1; i < 50; i++)
            {
                Assert::AreEqual(1, (*capturedPayloadCounts)[i], L"Wrong number of items in single-item-payloads");
            }

            for (int i = 0; i < 50; i++)
            {
                m_client->Track(L"TrackEvent", nullptr);
            }

            SpinWaitForItemCount(itemsSuccessfullyUploaded, 150);

            Assert::AreEqual(52, (int)capturedPayloadCounts->size(), L"Wrong number of payloads sent");
            Assert::AreEqual(50, (*capturedPayloadCounts)[51], L"Wrong number of items in the third payload");

            m_client->Shutdown().wait();
            m_client = nullptr;
        }

        TEST_METHOD(ItemsThatFailAreRetriedMoreThanOnce)
        {
            int event1Count = 0;
            int event2Count = 0;
            int event3Count = 0;
            int itemCount = 0;

            m_client->SetUploadToServiceMock([&event1Count, &event2Count, &event3Count, &itemCount](auto, auto payloads, auto)
            {
                bool successful = true;

                for (auto item : MixpanelTests::CaptureRequestPayloads(payloads))
                {
                    auto asObject = static_cast<JsonObject^>(item);
                    auto eventName = asObject->GetNamedString(L"event");
                    if (eventName == L"TrackEvent1")
                    {
                        event1Count++;
                    }
                    else if (eventName == L"TrackEvent2")
                    {
                        event2Count++;
                        // Fail the batch the first two times
                        if (event2Count < 3)
                        {
                            successful = false;
                        }
                    }
                    else if (eventName == L"TrackEvent3")
                    {
                        event3Count++;
                    }

                    itemCount++;
                }

                return task_from_result(successful);
            });

            m_client->Track(L"TrackEvent1", nullptr);
            m_client->Track(L"TrackEvent2", nullptr);
            m_client->Track(L"TrackEvent3", nullptr);

            m_client->Start();

            SpinWaitForItemCount(itemCount, 7);

            Assert::AreEqual(2, event1Count, L"Should only see event 1 twice - once in first payload, second in individual payload");
            Assert::AreEqual(2, event3Count, L"Should only see event 3 once - once in first payload, second in individual payload");
            Assert::AreEqual(3, event2Count, L"Event 2 should have been retried twice, and once successfully");

            m_client->Shutdown().wait();
            m_client = nullptr;
        }

        TEST_METHOD(DurationIsAutomaticallyAttached)
        {
            vector<vector<IJsonValue^>> capturedPayloads;
            m_client->SetUploadToServiceMock([&capturedPayloads](auto, auto payloads, auto)
            {
                capturedPayloads.push_back(MixpanelTests::CaptureRequestPayloads(payloads));
                return task_from_result(true);
            });

            m_client->ConfigureForTesting(DEFAULT_IDLE_TIMEOUT, 1);

            m_client->Start();
            auto now = chrono::steady_clock::now();
            SetNextClockAccessTime_MixpanelClient(now);
            m_client->StartTimedEvent(L"TestEvent");

            SetNextClockAccessTime_MixpanelClient(now + 1000ms);
            m_client->Track(L"TestEvent", nullptr);

            this_thread::sleep_for(DEFAULT_IDLE_TIMEOUT);

            Assert::AreEqual(1, (int)capturedPayloads.size(), L"Wrong number of payloads sent");
            Assert::AreEqual(1, (int)(capturedPayloads[0].size()), L"Wrong number of items in the first payload");
            
            auto jsonObjectPayload = dynamic_cast<JsonObject^>(capturedPayloads[0][0]);
            Assert::IsTrue(jsonObjectPayload->HasKey(L"properties"), L"properties weren't attached");

            auto propertiesPayload = jsonObjectPayload->GetNamedObject(L"properties");
            auto attachedDuration = propertiesPayload->GetNamedNumber(L"duration");
            Assert::AreEqual(1000.0, attachedDuration, L"Incorrect duration attached");
        }

        TEST_METHOD(SessionPropertiesAreAutomaticallyAttached)
        {
            vector<vector<IJsonValue^>> capturedPayloads;
            m_client->SetUploadToServiceMock([&capturedPayloads](auto, auto payloads, auto)
            {
                capturedPayloads.push_back(MixpanelTests::CaptureRequestPayloads(payloads));
                return task_from_result(true);
            });

            m_client->ConfigureForTesting(DEFAULT_IDLE_TIMEOUT, 1);
            m_client->Start();

            auto now = chrono::steady_clock::now();
            SetNextClockAccessTime_MixpanelClient(now);

            m_client->AutomaticallyTrackSessions = true;
            m_client->StartSessionTracking();

            m_client->SetSessionPropertyAsBoolean("SessionPropertyA", true);
            
            SetNextClockAccessTime_MixpanelClient(now + 1000ms);
            m_client->EndSessionTracking();

            this_thread::sleep_for(DEFAULT_IDLE_TIMEOUT);

            Assert::AreEqual(1, (int)capturedPayloads.size(), L"Wrong number of payloads sent");
            Assert::AreEqual(1, (int)(capturedPayloads[0].size()), L"Wrong number of items in the first payload");

            auto jsonObjectPayload = dynamic_cast<JsonObject^>(capturedPayloads[0][0]);
            Assert::IsTrue(jsonObjectPayload->HasKey(L"properties"), L"properties weren't attached");

            auto propertiesPayload = jsonObjectPayload->GetNamedObject(L"properties");
            Assert::IsTrue(propertiesPayload->HasKey(L"duration"), L"No Duration found");

            auto attachedDuration = propertiesPayload->GetNamedNumber(L"duration");
            Assert::AreEqual(1000.0, attachedDuration, L"Incorrect duration attached");

            Assert::IsTrue(propertiesPayload->HasKey(L"SessionPropertyA"), L"Session property not attached");
            Assert::IsTrue(propertiesPayload->GetNamedBoolean("SessionPropertyA"), L"Wrong session property value");
        }

        TEST_METHOD(SessionPropertiesClearedAfterEndingTheSession)
        {
            vector<vector<IJsonValue^>> capturedPayloads;
            m_client->SetUploadToServiceMock([&capturedPayloads](auto, auto payloads, auto)
            {
                capturedPayloads.push_back(MixpanelTests::CaptureRequestPayloads(payloads));
                return task_from_result(true);
            });

            m_client->ConfigureForTesting(DEFAULT_IDLE_TIMEOUT, 1);
            m_client->Start();

            auto now = chrono::steady_clock::now();
            SetNextClockAccessTime_MixpanelClient(now);

            m_client->AutomaticallyTrackSessions = true;
            m_client->StartSessionTracking();

            m_client->SetSessionPropertyAsBoolean("SessionPropertyA", true);

            SetNextClockAccessTime_MixpanelClient(now + 1000ms);
            m_client->EndSessionTracking();

            this_thread::sleep_for(DEFAULT_IDLE_TIMEOUT);

            Assert::AreEqual(1, (int)capturedPayloads.size(), L"Wrong number of payloads sent");
            Assert::AreEqual(1, (int)(capturedPayloads[0].size()), L"Wrong number of items in the first payload");

            auto jsonObjectPayload = dynamic_cast<JsonObject^>(capturedPayloads[0][0]);
            Assert::IsTrue(jsonObjectPayload->HasKey(L"properties"), L"properties weren't attached");

            auto propertiesPayload = jsonObjectPayload->GetNamedObject(L"properties");
            Assert::IsTrue(propertiesPayload->HasKey(L"SessionPropertyA"), L"Session property not attached");
            Assert::IsTrue(propertiesPayload->GetNamedBoolean("SessionPropertyA"), L"Wrong session property value");

            capturedPayloads.clear();

            // Start and end a second session and expect no session property to be attached
            SetNextClockAccessTime_MixpanelClient(now + 2000ms);
            m_client->StartSessionTracking();

            SetNextClockAccessTime_MixpanelClient(now + 2500ms);
            m_client->EndSessionTracking();

            this_thread::sleep_for(DEFAULT_IDLE_TIMEOUT);

            Assert::AreEqual(1, (int)capturedPayloads.size(), L"Wrong number of payloads sent");
            Assert::AreEqual(1, (int)(capturedPayloads[0].size()), L"Wrong number of items in the first payload");

            jsonObjectPayload = dynamic_cast<JsonObject^>(capturedPayloads[0][0]);
            Assert::IsTrue(jsonObjectPayload->HasKey(L"properties"), L"properties weren't attached");

            propertiesPayload = jsonObjectPayload->GetNamedObject(L"properties");
            Assert::IsFalse(propertiesPayload->HasKey(L"SessionPropertyA"), L"Session property was attached");
        }
#pragma endregion

#pragma region Identity and People tests
        TEST_METHOD(NoIdentityFoundWhenNotSet)
        {
            Assert::IsFalse(m_client->HasUserIdentity());
        }

        TEST_METHOD(CanSetExplicitClientIdentityAndHasIdentity)
        {
            auto USER_IDENTITY = StringReference(L"ExplicitIdentity");
            m_client->SetUserIdentityExplicitly(USER_IDENTITY);

            Assert::IsTrue(m_client->HasUserIdentity());

            auto userIdentity = m_client->GetDistinctId();
            Assert::AreEqual(StringReference(USER_IDENTITY), userIdentity, "Stored User Identity was wrong");
        }

        TEST_METHOD(ClientIdentityCanBeCleared)
        {
            auto USER_IDENTITY = StringReference(L"UserIdentityToBeCleared");
            m_client->SetUserIdentityExplicitly(USER_IDENTITY);

            Assert::IsTrue(m_client->HasUserIdentity(), L"Expected to find identity");
            Assert::AreEqual(USER_IDENTITY, m_client->GetDistinctId(), L"Wrong identity stored");

            m_client->ClearUserIdentity();

            Assert::IsFalse(m_client->HasUserIdentity(), L"User identity was not cleared");
        }

        TEST_METHOD(ClientIdentityIsPersistedAcrossInstances)
        {
            auto USER_IDENTITY = StringReference(L"PersistedExplicitIdentity");
            auto client = ref new MixpanelClient(StringReference(DEFAULT_TOKEN));
            client->SetUserIdentityExplicitly(USER_IDENTITY);

            client = ref new MixpanelClient(StringReference(DEFAULT_TOKEN));
            auto storedUserIdentity = client->GetDistinctId();
            Assert::AreEqual(USER_IDENTITY, storedUserIdentity, L"User Identity wasn't persisted");

            // Since we don't want to rely on the destruction of the
            // super properties in the clear method, lets just clear the local state
            client->ClearUserIdentity();

            Assert::IsFalse(client->HasUserIdentity(), L"Expected local data to be empty");
        }

        TEST_METHOD(ClientIdentityClearedAcrossInstances)
        {
            auto USER_IDENTITY = StringReference(L"PersistedExplicitIdentity");
            auto client = ref new MixpanelClient(StringReference(DEFAULT_TOKEN));
            client->SetUserIdentityExplicitly(USER_IDENTITY);

            client = ref new MixpanelClient(StringReference(DEFAULT_TOKEN));
            client->ClearUserIdentity();

            Assert::IsFalse(client->HasUserIdentity(), L"Expected local data to be empty");

            client = ref new MixpanelClient(StringReference(DEFAULT_TOKEN));
            Assert::IsFalse(client->HasUserIdentity(), L"Expected local data to be empty in a new instance");
        }

        TEST_METHOD(CanAutomaticallyGenerateUserIdentity)
        {
            Assert::IsFalse(m_client->HasUserIdentity(), L"Expected no identity");
            m_client->GenerateAndSetUserIdentity();
            Assert::IsTrue(m_client->HasUserIdentity(), L"User Identity should have been set");
            auto clientId = m_client->GetDistinctId();
            Assert::IsFalse(clientId->IsEmpty(), L"Retrived ID was empty");
        }

        TEST_METHOD(ClearingSuperPropertiesKeepsUserIdentity)
        {
            auto USER_IDENTITY = StringReference(L"UserIdenitySavedWhenClearing");
            m_client->SetUserIdentityExplicitly(USER_IDENTITY);

            m_client->SetSuperPropertyAsString(L"SuperPropertyA", L"SuperValueA");

            // Validate that Super Property is present
            Assert::IsTrue(m_client->HasSuperProperty(L"SuperPropertyA"), L"No SuperPropertyA in properties payload");
            Assert::AreEqual(L"SuperValueA", m_client->GetSuperPropertyAsString(L"SuperPropertyA"), L"SuperPropertyA had incorrect value");

            // Clear the super properties, and generate the payload again
            m_client->ClearSuperProperties();

            // Validate that Super Property isn't present
            Assert::IsFalse(m_client->HasSuperProperty(L"SuperPropertyA"), L"SuperPropertyA present, when it should have been cleared");

            // Validate that User Identity is present
            Assert::IsTrue(m_client->HasUserIdentity(), L"User Identity not present, when it should saved");
        }

        TEST_METHOD(ExceptionThrownGettingEngagePropertiesWhenNoIdentitySet)
        {
            bool exceptionThrown = false;
            try
            {
                m_client->GetEngageProperties(nullptr);
            }
            catch(InvalidArgumentException^ ex)
            {
                exceptionThrown = true;
            }

            Assert::IsTrue(exceptionThrown, L"No exception thrown when an identity isn't set");
        }

        TEST_METHOD(IdentityIncludedWhenGettingEngagePropertiesWithEmptyOptions)
        {
            constexpr auto IDENTITY = L"IDENTITY_INCLUDED_WHEN_GETTING_PROPERTIES";
            m_client->SetUserIdentityExplicitly(StringReference(IDENTITY));

            auto properties = m_client->GetEngageProperties(nullptr);
            Assert::IsTrue(properties->HasKey("$distinct_id"), L"No distinct ID set");
            Assert::AreEqual(StringReference(IDENTITY),
                             static_cast<String^>(properties->Lookup(StringReference(DISTINCT_ENGAGE_KEY))),
                             L"Key doesn't match");

            Assert::IsTrue(properties->HasKey(L"$token"), L"Token not included in property set");
        }

        TEST_METHOD(OptionsIncludedWhenGettingEnageProperties)
        {
            auto calendar = ref new Windows::Globalization::Calendar();
            m_client->GenerateAndSetUserIdentity();

            auto options = ref new PropertySet();
            options->Insert(EngageOptionNames::Ip, L"512.512.512.512");
            options->Insert(EngageOptionNames::Time, calendar->GetDateTime());
            options->Insert(EngageOptionNames::IgnoreTime, true);
            options->Insert(EngageOptionNames::IgnoreAlias, true);

            auto properties = m_client->GetEngageProperties(options);

            Assert::IsTrue(properties->HasKey(StringReference(DISTINCT_ENGAGE_KEY)), L"No identity included");
            Assert::IsTrue(properties->HasKey(StringReference(TOKEN_ENGAGE_KEY)), L"Token not included in property set");

            Assert::IsTrue(properties->HasKey(EngageOptionNames::Ip), L"IP Option not found");
            Assert::IsTrue(properties->HasKey(EngageOptionNames::Time), L"Time Option not found");
            Assert::IsTrue(properties->HasKey(EngageOptionNames::IgnoreTime), L"IgnoreTime Option not found");
            Assert::IsTrue(properties->HasKey(EngageOptionNames::IgnoreAlias), L"IgnoreAlias not found");
        }

        void TestEngagePayloadOption(EngageOperationType operation, String^ payloadPropertyName)
        {
            constexpr auto KEY = L"MyKey";
            constexpr auto VALUE = L"MyValue";

            m_client->GenerateAndSetUserIdentity();
            auto properties = m_client->GetEngageProperties(nullptr);
            auto values = ref new ValueSet();
            values->Insert(StringReference(KEY), StringReference(VALUE));

            JsonObject^ payload = MixpanelClient::GenerateEngageJsonPayload(operation, values, properties);

            Assert::IsTrue(payload->HasKey(StringReference(DISTINCT_ENGAGE_KEY)), L"No distinct value found");
            Assert::IsTrue(payload->HasKey(StringReference(TOKEN_ENGAGE_KEY)), L"No token value found");

            Assert::IsTrue(payload->HasKey(payloadPropertyName), L"Has no set object");

            auto jsonValues = payload->GetNamedObject(payloadPropertyName);
            Assert::AreEqual(1, (int)jsonValues->Size, L"Wrong number of values");
            Assert::IsTrue(jsonValues->HasKey(StringReference(KEY)), L"Custom Value not found");
            Assert::AreEqual(StringReference(VALUE), jsonValues->GetNamedString(StringReference(KEY)), L"Value of single key didn't match");
        }

        TEST_METHOD(GeneratingEngagePayloadWithSetIncludesValues)
        {
            TestEngagePayloadOption(EngageOperationType::Set, L"$set");
        }

        TEST_METHOD(GeneratingEngagePayloadWithSetOnceIncludesValues)
        {
            TestEngagePayloadOption(EngageOperationType::Set_Once, L"$set_once");
        }

        TEST_METHOD(GeneratingEngagePayloadWithAppendIncludesValues)
        {
            TestEngagePayloadOption(EngageOperationType::Append, L"$append");
        }

        TEST_METHOD(GeneratingEngagePayloadWithRemoveIncludesValues)
        {
            TestEngagePayloadOption(EngageOperationType::Remove, L"$remove");
        }

        TEST_METHOD(GeneratingPayloadWithAddIncludesValues)
        {
            constexpr auto KEY = L"MyKey";
            constexpr auto VALUE = 3.14;

            m_client->GenerateAndSetUserIdentity();
            auto properties = m_client->GetEngageProperties(nullptr);
            auto values = ref new ValueSet();
            values->Insert(StringReference(KEY), VALUE);

            JsonObject^ payload = MixpanelClient::GenerateEngageJsonPayload(EngageOperationType::Add, values, properties);

            Assert::IsTrue(payload->HasKey(StringReference(DISTINCT_ENGAGE_KEY)), L"No distinct value found");
            Assert::IsTrue(payload->HasKey(StringReference(TOKEN_ENGAGE_KEY)), L"No token value found");

            Assert::IsTrue(payload->HasKey(L"$add"), L"Has no set object");

            auto jsonValues = payload->GetNamedObject(L"$add");
            Assert::AreEqual(1, (int)jsonValues->Size, L"Wrong number of values");
            Assert::IsTrue(jsonValues->HasKey(StringReference(KEY)), L"Custom Value not found");
            Assert::AreEqual(VALUE, jsonValues->GetNamedNumber(StringReference(KEY)), L"Value of single key didn't match");
        }

        TEST_METHOD(GeneratingPayloadWithUnionIncludesValues)
        {
            constexpr auto KEY = L"MyKey";
            Vector<int>^ VALUE = ref new Vector<int>({ 1, 2, 3 });

            m_client->GenerateAndSetUserIdentity();
            auto properties = m_client->GetEngageProperties(nullptr);
            auto values = ref new PropertySet();
            values->Insert(StringReference(KEY), VALUE);

            JsonObject^ payload = MixpanelClient::GenerateEngageJsonPayload(EngageOperationType::Union, values, properties);

            Assert::IsTrue(payload->HasKey(StringReference(DISTINCT_ENGAGE_KEY)), L"No distinct value found");
            Assert::IsTrue(payload->HasKey(StringReference(TOKEN_ENGAGE_KEY)), L"No token value found");

            Assert::IsTrue(payload->HasKey(L"$union"), L"Has no operation object");

            auto jsonValues = payload->GetNamedObject(L"$union");
            Assert::AreEqual(1, (int)jsonValues->Size, L"Wrong number of values");
            Assert::IsTrue(jsonValues->HasKey(StringReference(KEY)), L"Custom Value not found");

            JsonArray^ unionValues = jsonValues->GetNamedArray(StringReference(KEY));
            Assert::AreEqual(3, (int)unionValues->Size, L"Wrong Number value in JSON payload");
            Assert::AreEqual((double)1, unionValues->GetNumberAt(0), L"First value wrong");
            Assert::AreEqual((double)2, unionValues->GetNumberAt(1), L"Second value wrong");
            Assert::AreEqual((double)3, unionValues->GetNumberAt(2), L"Third value wrong");
        }

        TEST_METHOD(GeneratingPayloadForUnsetOnlyIncludesSingleArrayInOperationProperty)
        {
            constexpr auto KEY_1 = L"Key1";
            constexpr auto KEY_2 = L"Key2";

            m_client->GenerateAndSetUserIdentity();
            auto properties = m_client->GetEngageProperties(nullptr);
            auto values = ref new PropertySet();
            values->Insert(StringReference(KEY_1), L"AValue");
            values->Insert(StringReference(KEY_2), nullptr);

            JsonObject^ payload = MixpanelClient::GenerateEngageJsonPayload(EngageOperationType::Unset, values, properties);

            Assert::IsTrue(payload->HasKey(StringReference(DISTINCT_ENGAGE_KEY)), L"No distinct value found");
            Assert::IsTrue(payload->HasKey(StringReference(TOKEN_ENGAGE_KEY)), L"No token value found");

            Assert::IsTrue(payload->HasKey(L"$unset"), L"Has no operation object");

            auto unsetArray = payload->GetNamedArray(L"$unset");
            Assert::AreEqual(2, (int)unsetArray->Size, L"Wrong number of values");
            bool foundKey1 = false;
            bool foundKey2 = false;

            // Why are we looping, and not using JsonArray::IndexOf? Because
            // when you do an index of with a JsonString, it doesn't find it.
            // Even though, you know, it's in the bloody array.
            for (unsigned int i = 0; i < unsetArray->Size; i++)
            {
                auto value = unsetArray->GetStringAt(i);
                if (value == StringReference(KEY_1))
                {
                    foundKey1 = true;
                }

                if (value == StringReference(KEY_1))
                {
                    foundKey2 = true;
                }
            }

            Assert::IsTrue(foundKey1, L"First Key Not found");
            Assert::IsTrue(foundKey2, L"Second Key Not found");
        }

        TEST_METHOD(GeneratingPayloadForDeleteProfileWithValuesThrows)
        {
            constexpr auto KEY_1 = L"Key1";

            m_client->GenerateAndSetUserIdentity();
            auto properties = m_client->GetEngageProperties(nullptr);
            auto values = ref new PropertySet();
            values->Insert(StringReference(KEY_1), L"AValue");

            bool exceptionHappened = false;
            try
            {
                JsonObject^ payload = MixpanelClient::GenerateEngageJsonPayload(EngageOperationType::DeleteProfile, values, properties);
            }
            catch (InvalidArgumentException^)
            {
                exceptionHappened = true;
            }

            Assert::IsTrue(exceptionHappened, L"No exception thrown when delete provided with values");
        }

        TEST_METHOD(GeneratingPayloadForDeleteOnlyIncludesEmptyStringForOperationValue)
        {

            m_client->GenerateAndSetUserIdentity();
            auto properties = m_client->GetEngageProperties(nullptr);

            JsonObject^ payload = MixpanelClient::GenerateEngageJsonPayload(EngageOperationType::DeleteProfile, nullptr, properties);

            Assert::IsTrue(payload->HasKey(StringReference(DISTINCT_ENGAGE_KEY)), L"No distinct value found");
            Assert::IsTrue(payload->HasKey(StringReference(TOKEN_ENGAGE_KEY)), L"No token value found");

            Assert::IsTrue(payload->HasKey(L"$delete"), L"Has no operation object");

            auto deleteOperationValue = payload->GetNamedString(L"$delete");
            Assert::IsTrue(deleteOperationValue->IsEmpty(), L"Operation shouldn't have had a value");
        }

        TEST_METHOD(GeneratingPayloadWithAddFailsWithNonNumericValues)
        {
            constexpr auto KEY = L"MyKey";
            constexpr auto VALUE = L"MyValue";

            m_client->GenerateAndSetUserIdentity();
            auto properties = m_client->GetEngageProperties(nullptr);
            auto values = ref new ValueSet();
            values->Insert(StringReference(KEY), StringReference(VALUE));

            bool exceptionHappened = false;

            try
            {
                JsonObject^ payload = MixpanelClient::GenerateEngageJsonPayload(EngageOperationType::Add, values, properties);
            }
            catch (InvalidCastException^)
            {
                exceptionHappened = true;
            }

            Assert::IsTrue(exceptionHappened, L"Exception didn't happen");
        }

        TEST_METHOD(DeleteProfileOnlyContainsRequiredInformation)
        {
            vector<vector<IJsonValue^>> capturedPayloads;
            m_client->SetUploadToServiceMock([&capturedPayloads](auto, auto payloads, auto)
            {
                capturedPayloads.push_back(MixpanelTests::CaptureRequestPayloads(payloads));
                return task_from_result(true);
            });

            m_client->GenerateAndSetUserIdentity();
            m_client->ConfigureForTesting(DEFAULT_IDLE_TIMEOUT, 1);

            m_client->Start();
            m_client->DeleteProfile();

            this_thread::sleep_for(DEFAULT_IDLE_TIMEOUT);

            Assert::AreEqual(1, (int)capturedPayloads.size(), L"Wrong number of payloads sent");
            Assert::AreEqual(1, (int)(capturedPayloads[0].size()), L"Wrong number of items in the first payload");

            auto jsonObjectPayload = dynamic_cast<JsonObject^>(capturedPayloads[0][0]);
            Assert::AreEqual(3, (int)jsonObjectPayload->Size, L"Wrong number of properties");

            Assert::IsTrue(jsonObjectPayload->HasKey(L"$token"), L"Token missing");
            Assert::IsTrue(jsonObjectPayload->HasKey(L"$distinct_id"), L"User Identity Missing");
            Assert::IsTrue(jsonObjectPayload->HasKey(L"$delete"), L"Delete Operation Missing");

            Assert::IsTrue(jsonObjectPayload->GetNamedString(L"$delete")->IsEmpty(), L"Delete operation contained data, it shouldn't");
        }
#pragma endregion
    };
}
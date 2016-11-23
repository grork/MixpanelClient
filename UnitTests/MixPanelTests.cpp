#include "pch.h"
#include "CppUnitTest.h"
#include "MixpanelClient.h"
#include "AsyncHelper.h"

using namespace Platform;
using namespace CodevoidN::Tests::Utilities;
using namespace CodevoidN::Utilities::Mixpanel;
using namespace concurrency;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Windows::Data::Json;
using namespace Windows::Foundation::Collections;
using namespace Windows::Storage;

#define DEFAULT_TOKEN L"DEFAULT_TOKEN"

namespace CodevoidN { namespace  Tests { namespace Mixpanel
{
    TEST_CLASS(MixpanelTests)
    {
    private:
        MixpanelClient^ m_client;

    public:
        TEST_METHOD_INITIALIZE(InitializeClass)
        {
            m_client = ref new MixpanelClient(DEFAULT_TOKEN);
            
            // Disable Persistence of Super properties
            // to help maintain these tests as stateless
            m_client->PersistSuperPropertiesToApplicationData = false;
        }

        TEST_METHOD(ConstructorThrowsWhenNoTokenProvided)
        {
            MixpanelClient^ constructedClient;
            bool exceptionSeen = false;

            try
            {
                constructedClient = ref new MixpanelClient(nullptr);
            }
            catch (InvalidArgumentException^ e)
            {
                exceptionSeen = true;
            }

            Assert::IsTrue(exceptionSeen, L"Didn't get exception while constructing client w/out a token");
            Assert::IsNull(constructedClient, L"Client was constructed despite not being provided token");
        }

        TEST_METHOD(TrackThrowsWithMissingEventName)
        {
            bool exceptionThrown = false;

            try
            {
                m_client->Track(nullptr, ref new ValueSet());
            }
            catch (NullReferenceException^ ex)
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
            catch(InvalidCastException^ ex)
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

        TEST_METHOD(CanSendTrackRequest)
        {
            IPropertySet^ properties = ref new PropertySet();
            properties->Insert(L"StringValue", L"Value");
            
            m_client->Track(L"TestEvent", properties);
        }
    };
} } }
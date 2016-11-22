#include "pch.h"
#include "CppUnitTest.h"
#include "MixpanelClient.h"

using namespace Platform;
using namespace CodevoidN::Utilities::Mixpanel;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Windows::Data::Json;
using namespace Windows::Foundation::Collections;

namespace CodevoidN { namespace Tests { namespace Mixpanel
{
    TEST_CLASS(MixpanelTests)
    {
    private:
        MixpanelClient^ m_client;

    public:
        TEST_METHOD_INITIALIZE(InitializeClass)
        {
            m_client = ref new MixpanelClient();
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

            try
            {
                m_client->GenerateJsonPayload("TestEventName", properties);
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

            bool exceptionThrown = false;
            try
            {
                m_client->GenerateJsonPayload("TestEventName", properties);
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

            auto result = m_client->GenerateJsonPayload("TestEventName", properties);

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
            try
            {
                m_client->GenerateJsonPayload("TestEventName", properties);
            }
            catch (InvalidArgumentException^ ex)
            {
                exceptionThrown = true;
            }

            Assert::IsTrue(exceptionThrown, L"Didn't get exception for mp_ prefixed property in property set");
        }
    };
} } }
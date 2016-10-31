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
    };
} } }
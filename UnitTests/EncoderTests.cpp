#include "pch.h"
#include "CppUnitTest.h"
#include "PayloadEncoder.h"

using namespace Platform;
using namespace CodevoidN::Utilities::Mixpanel;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Windows::Data::Json;
using namespace Windows::Foundation::Collections;

namespace CodevoidN { namespace Tests { namespace Mixpanel {
    TEST_CLASS(EncoderTests)
    {
    public:
        TEST_METHOD(CanEncodePayloadCorrectly)
        {
            JsonObject^ payload = JsonObject::Parse(L"{ \"event\": \"Signed Up\", \"properties\": { \"distinct_id\": \"13793\", \"token\": \"e3bc4100330c35722740fb8c6f5abddc\", \"Referred By\": \"Friend\" } }");
            auto encodedPayload = EncodeJson(payload);
            Assert::AreEqual(L"eyJldmVudCI6IlNpZ25lZCBVcCIsInByb3BlcnRpZXMiOnsiZGlzdGluY3RfaWQiOiIxMzc5MyIsInRva2VuIjoiZTNiYzQxMDAzMzBjMzU3MjI3NDBmYjhjNmY1YWJkZGMiLCJSZWZlcnJlZCBCeSI6IkZyaWVuZCJ9fQ==", encodedPayload, "Not equal");
        }
    };
} } }
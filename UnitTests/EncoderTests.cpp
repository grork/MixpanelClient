#include "pch.h"
#include "CppUnitTest.h"
#include "PayloadEncoder.h"

using namespace Platform;
using namespace Codevoid::Utilities::Mixpanel;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;

namespace Codevoid::Tests::Mixpanel {
    TEST_CLASS(EncoderTests)
    {
    public:
        TEST_METHOD(CanEncodePayloadCorrectly)
        {
            JsonObject^ payload = JsonObject::Parse(L"{ \"event\": \"Signed Up\", \"properties\": { \"distinct_id\": \"13793\", \"token\": \"e3bc4100330c35722740fb8c6f5abddc\", \"Referred By\": \"Friend\" } }");
            auto encodedPayload = EncodeJson(payload);
            Assert::AreEqual(L"eyJldmVudCI6IlNpZ25lZCBVcCIsInByb3BlcnRpZXMiOnsiZGlzdGluY3RfaWQiOiIxMzc5MyIsInRva2VuIjoiZTNiYzQxMDAzMzBjMzU3MjI3NDBmYjhjNmY1YWJkZGMiLCJSZWZlcnJlZCBCeSI6IkZyaWVuZCJ9fQ==", encodedPayload, "Not equal");
        }

        TEST_METHOD(EncodeDateTimeWithMixpanelFormat)
        {
            _SYSTEMTIME time = {
                2018,
                8,
                5,
                31,
                16,
                9,
                6,
                0
            };

            FILETIME ftime;
            Assert::IsTrue(SystemTimeToFileTime(&time, &ftime), L"File time didn't convert");
            //use _ULARGE_INTEGER to get a uint64 to set the DateTime struct to
            _ULARGE_INTEGER utime = { ftime.dwLowDateTime, ftime.dwHighDateTime };
            DateTime dtime; dtime.UniversalTime = utime.QuadPart;

            String^ converted = DateTimeToMixpanelDateFormat(dtime);
            Assert::AreEqual("2018-08-31T16:09:06", converted, L"DateTime didn't convert correctly");
        }
    };
}
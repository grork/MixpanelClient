#include "pch.h"
#include "CppUnitTest.h"
#include "RequestHelper.h"

using namespace CodevoidN::Utilities::Mixpanel;
using namespace concurrency;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Platform;
using namespace Platform::Collections;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

namespace CodevoidN { namespace Tests { namespace Mixpanel {
    TEST_CLASS(RequestHelperTests)
    {
    public:
        TEST_METHOD(RequestIndicatesFailureWhenCallingNonExistantEndPoint)
        {
			RequestHelper rh;
            auto payload = ref new Map<String^, IJsonValue^>();
            auto wasSuccessful = rh.PostRequest(ref new Uri(L"https://fake.codevoid.net"), payload).get();
            Assert::IsFalse(wasSuccessful);
        }

        TEST_METHOD(CanMakeRequestToPlaceholderService)
        {
			RequestHelper rh;
            auto payload = ref new Map<String^, IJsonValue^>();
            payload->Insert(L"data", JsonObject::Parse("{ \"data\": 0 }"));
            auto wasSuccessful = rh.PostRequest(ref new Uri(L"https://jsonplaceholder.typicode.com/posts"), payload).get();
            Assert::IsTrue(wasSuccessful);
        }
    };
} } }
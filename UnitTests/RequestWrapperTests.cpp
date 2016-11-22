#include "pch.h"
#include "CppUnitTest.h"
#include "RequestHelper.h"

using namespace CodevoidN::Utilities::Mixpanel;
using namespace concurrency;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Platform;
using namespace Windows::Foundation;

namespace CodevoidN { namespace Tests { namespace Mixpanel {
    TEST_CLASS(RequestHelperTests)
    {
    public:
        TEST_METHOD(CanConstructRequestHelper)
        {
            auto rh = ref new RequestHelper();
        }

        TEST_METHOD(RequestIndicatesFailureWhenFailing)
        {
            auto rh = ref new RequestHelper();
            auto wasSuccessful = rh->SendRequest(ref new Uri(L"https://fake.codevoid.net")).get();
            Assert::IsFalse(wasSuccessful);
        }

        TEST_METHOD(CanMakeRequestToPlaceholdService)
        {
            auto rh = ref new RequestHelper();
            auto wasSuccessful = rh->SendRequest(ref new Uri(L"https://jsonplaceholder.typicode.com/posts")).get();
            Assert::IsTrue(wasSuccessful);
        }
    };
} } }
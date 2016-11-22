#include <experimental\resumable>
#include <pplawait.h>
#include "pch.h"
#include "CppUnitTest.h"
#include "RequestHelper.h"

using namespace CodevoidN::Utilities::Mixpanel;
using namespace concurrency;
using namespace Platform;

namespace CodevoidN { namespace Tests { namespace Mixpanel {
    TEST_CLASS(RequestHelperTests)
    {
    public:
        TEST_METHOD(CanConstructRequestHelper)
        {
            auto rh = ref new RequestHelper();
        }
    };
} } }
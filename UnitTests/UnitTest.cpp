#include "pch.h"
#include "CppUnitTest.h"
#include "MixpanelClient.h"
#include "MixpanelApiKeys.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace CodevoidN { namespace Tests { namespace Mixpanel
{
    TEST_CLASS(ClientTests)
    {
    public:
        TEST_METHOD(CanInstantiate)
        {
            auto client = ref new CodevoidN::Utilities::Mixpanel::MixpanelClient();
            Assert::IsNotNull(client, "Couldn't instantiate client");
        }
    };
} } }
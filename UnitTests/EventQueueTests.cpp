#include "pch.h"
#include <memory>

#include "CppUnitTest.h"
#include "AsyncHelper.h"
#include "EventQueue.h"

using namespace Platform;
using namespace std;
using namespace CodevoidN::Tests::Utilities;
using namespace CodevoidN::Utilities::Mixpanel;
using namespace concurrency;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Windows::Data::Json;
using namespace Windows::Foundation::Collections;
using namespace Windows::Storage;

namespace CodevoidN { namespace  Tests { namespace Mixpanel
{
    TEST_CLASS(EventQueueTests)
    {
    private:
        shared_ptr<EventQueue> m_queue;

    public:
        TEST_METHOD_INITIALIZE(InitializeClass)
        {
            this->m_queue = make_shared<EventQueue>();
        }

        TEST_METHOD(CanQueueEvent)
        {
            auto result = this->m_queue->QueueEvent(ref new JsonObject());
            Assert::IsFalse(0 == result, L"Didn't get a token back from queueing the event");
            Assert::IsTrue(1 == this->m_queue->GetQueueLength(), L"Incorrect number of items");
        }

        TEST_METHOD(CanRemoveEvent)
        {
            auto result = this->m_queue->QueueEvent(ref new JsonObject());
            Assert::IsFalse(0 == result, L"Didn't get a token back from queueing the event");
            Assert::IsTrue(1 == this->m_queue->GetQueueLength(), L"Incorrect number of items");
            
            this->m_queue->RemoveEvent(result);

            Assert::IsTrue(0 == this->m_queue->GetQueueLength(), L"Expected 0 items in the list");
        }
    };
} } }
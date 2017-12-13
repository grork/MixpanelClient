#include "pch.h"

#include "CppUnitTest.h"
#include "BackgroundWorker.h"

using namespace Platform;
using namespace std;
using namespace CodevoidN::Utilities::Mixpanel;
using namespace concurrency;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Windows::Data::Json;
using namespace Windows::Foundation::Collections;
using namespace Windows::Storage;

namespace CodevoidN { namespace  Tests
{
    TEST_CLASS(BackgroundWorkerTests)
    {
    public:
        TEST_METHOD(CanInstantiateWorker)
        {
            BackgroundWorker<int> worker(
                [](const vector<shared_ptr<int>>&, const WorkerState&) {
                vector<shared_ptr<int>> v;
                return v;
            },
                [](const vector<shared_ptr<int>>&) {

            }, L"Foo");

            auto f = make_shared<int>(7);
            worker.AddWork(f);
        }
    };
} }
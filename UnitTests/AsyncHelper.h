//===============================================================================
// Part of this code is taken from http://hilo.codeplex.com
// License terms of Hilo project for this source code is located at
// http://hilo.codeplex.com/license
// A copy of MICROSOFT PATTERNS & PRACTICES LICENSE is also checkedin
// in file HiloLicense.txt with source at http://asynccppunittestlib.codeplex.com/
//===============================================================================
#pragma once
#include <ppl.h>
#include <ppltasks.h>
#include "pch.h"

using namespace Windows::Foundation;
using namespace concurrency;

// Macro to help w/ running tests code on the dispatcher.
// Example usage pattern:
// int count = 0;
// BEGIN_ON_DISPATCHER
// {
//     // This block runs on the Dispatcher
//     m_queue->EnableQueuingToStorage();
//     m_queue->QueueEvent(GenerateSamplePayload());
//     count++;
// }
// END_ON_DISPATCHER
//
// // Not on the dispatcher, but after the dispatcher operation completed
// Assert::AreEqual(1, count, L"Expected count to be increased");

#define BEGIN_ON_DISPATCHER AsyncHelper::RunSynced(Windows::ApplicationModel::Core::CoreApplication::MainView->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([&]()
#define END_ON_DISPATCHER )));

namespace CodevoidN { namespace Tests { namespace Utilities {
    class AsyncHelper
    {
    public:
        template<typename T>
        static T RunSynced(IAsyncOperation<T>^ action)
        {
            task<T> t(action);
            return RunSynced(t);
        };

        template<typename T, typename P>
        static T RunSynced(IAsyncOperationWithProgress<T, P>^ action)
        {
            task<T> t(action);
            return RunSynced(t);
        };

        template<typename P>
        static void RunSynced(IAsyncActionWithProgress<P>^ action)
        {
            task<void> t(action);
            RunSynced(t);
        };

        static void RunSynced(IAsyncAction^ action)
        {
            task<void> t(action);
            RunSynced(t);
        };

        template<typename T>
        static T RunSynced(task<T> t)
        {
            HANDLE hEvent = CreateEventEx(NULL, NULL, CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS);
            if (hEvent == NULL)
            {
                throw std::bad_alloc();
            }

            t.then([hEvent](task<T>)
            {
                SetEvent(hEvent);
            }, concurrency::task_continuation_context::use_arbitrary());

            // Spin wait and exercise message pump
            DWORD waitResult = STATUS_PENDING;
            while (waitResult != WAIT_OBJECT_0)
            {
                waitResult = WaitForSingleObjectEx(hEvent, 0, true);
            }

            CloseHandle(hEvent);

            return t.get();
        };
    };
} } }
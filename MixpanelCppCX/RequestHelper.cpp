#include "pch.h"
#include "RequestHelper.h"

using namespace CodevoidN::Utilities::Mixpanel;
using namespace concurrency;
using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Web::Http;
using namespace Windows::Web::Http::Headers;

RequestHelper::RequestHelper()
{
    this->UserAgent = ref new HttpProductInfoHeaderValue(L"Codevoid.Utilities.MixpanelClient", L"1.0");
}

task<bool> RequestHelper::PostRequest(_In_ Uri^ uri, IMap<String^, String^>^ payload)
{
    HttpClient^ client = ref new HttpClient();
    client->DefaultRequestHeaders->UserAgent->Append(this->UserAgent);

    try
    {
        auto content = ref new HttpFormUrlEncodedContent(payload);
        auto requestResult = co_await client->PostAsync(uri, content);
        return requestResult->IsSuccessStatusCode;
    }
    catch (...)
    {
        return false;
    }
}
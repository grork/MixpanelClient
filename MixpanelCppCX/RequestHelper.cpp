#include "pch.h"
#include "RequestHelper.h"

using namespace CodevoidN::Utilities::Mixpanel;
using namespace concurrency;
using namespace Windows::Foundation;
using namespace Windows::Web::Http;
using namespace Windows::Web::Http::Headers;

RequestHelper::RequestHelper()
{
    this->UserAgent = ref new HttpProductInfoHeaderValue(L"Codevoid.Utilities.MixpanelClient", L"1.0");
}

task<bool> RequestHelper::SendRequest(_In_ Uri^ uri)
{
    HttpClient^ client = ref new HttpClient();
    client->DefaultRequestHeaders->UserAgent->Append(this->UserAgent);

    try
    {
        auto requestResult = co_await client->GetAsync(uri);
        return requestResult->IsSuccessStatusCode;
    }
    catch (...)
    {
        return false;
    }
}
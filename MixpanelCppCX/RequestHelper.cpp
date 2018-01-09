#include "pch.h"
#include "PayloadEncoder.h"
#include "RequestHelper.h"

using namespace CodevoidN::Utilities::Mixpanel;
using namespace concurrency;
using namespace Platform;
using namespace Platform::Collections;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Web::Http;
using namespace Windows::Web::Http::Headers;

RequestHelper::RequestHelper()
{
	this->UserAgent(ref new HttpProductInfoHeaderValue(L"Codevoid.Utilities.MixpanelClient", L"1.0"));
}

HttpProductInfoHeaderValue^ RequestHelper::UserAgent()
{
	return m_userAgent;
}

void RequestHelper::UserAgent(Windows::Web::Http::Headers::HttpProductInfoHeaderValue^ userAgent)
{
	m_userAgent = userAgent;
}

task<bool> RequestHelper::PostRequest(_In_ Uri^ uri, IMap<String^, IJsonValue^>^ payload)
{
    HttpClient^ client = ref new HttpClient();
    client->DefaultRequestHeaders->UserAgent->Append(this->UserAgent());

    Map<String^, String^>^ encodedPayload = ref new Map<String^, String^>();

    for (auto&& pair : payload)
    {
        encodedPayload->Insert(pair->Key, EncodeJson(pair->Value));
    }

    try
    {
        auto content = ref new HttpFormUrlEncodedContent(encodedPayload);
        auto requestResult = co_await client->PostAsync(uri, content);
        return requestResult->IsSuccessStatusCode;
    }
    catch (...)
    {
        return false;
    }
}
#pragma once

namespace CodevoidN { namespace Utilities { namespace Mixpanel {
    class RequestHelper
    {
	public:
        RequestHelper();

        Windows::Web::Http::Headers::HttpProductInfoHeaderValue^ UserAgent();
		void UserAgent(Windows::Web::Http::Headers::HttpProductInfoHeaderValue^ userAgent);
        concurrency::task<bool> PostRequest(_In_ Windows::Foundation::Uri^ uri, Windows::Foundation::Collections::IMap<Platform::String^, Platform::String^>^ payload);

	private:
		Windows::Web::Http::Headers::HttpProductInfoHeaderValue^ m_userAgent;
    };
} } }
#pragma once

namespace CodevoidN { namespace Utilities { namespace Mixpanel {
	class IRequestHelper
	{
		virtual Windows::Web::Http::Headers::HttpProductInfoHeaderValue^ UserAgent() = 0;
		virtual void UserAgent(Windows::Web::Http::Headers::HttpProductInfoHeaderValue^ userAgent) = 0;
		concurrency::task<bool> PostRequest(_In_ Windows::Foundation::Uri^ uri, Windows::Foundation::Collections::IMap<Platform::String^, Platform::String^>^ payload);
	};

    class RequestHelper : public IRequestHelper
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
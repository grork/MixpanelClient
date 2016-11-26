#pragma once

namespace CodevoidN { namespace Utilities { namespace Mixpanel {
    ref class RequestHelper
    {
    internal:
        RequestHelper();

        property Windows::Web::Http::Headers::HttpProductInfoHeaderValue^ UserAgent;
        concurrency::task<bool> PostRequest(_In_ Windows::Foundation::Uri^ uri, Windows::Foundation::Collections::IMap<Platform::String^, Platform::String^>^ payload);
    };
} } }
#pragma once

namespace CodevoidN { namespace Utilities { namespace Mixpanel {
    ref class RequestHelper
    {
    internal:
        RequestHelper();

        property Windows::Web::Http::Headers::HttpProductInfoHeaderValue^ UserAgent;
        concurrency::task<bool> SendRequest(_In_ Windows::Foundation::Uri^ uri);
    };
} } }
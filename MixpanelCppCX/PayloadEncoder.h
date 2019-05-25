#pragma once

namespace Codevoid::Utilities::Mixpanel {
    Platform::String^ EncodeJson(Windows::Data::Json::IJsonValue^ payload);
    Platform::String^ DateTimeToMixpanelDateFormat(const Windows::Foundation::DateTime time);
}
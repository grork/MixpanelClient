#pragma once

namespace CodevoidN { namespace Utilities { namespace Mixpanel {
    public ref class MixpanelClient sealed
    {
    public:
        MixpanelClient(_In_ ::Platform::String^ token);
        void Track(_In_ ::Platform::String^ name, _In_ Windows::Foundation::Collections::IPropertySet^ properties);

    internal:
        Windows::Data::Json::JsonObject^ GenerateJsonPayloadFromPropertySet(_In_ Windows::Foundation::Collections::IPropertySet^ properties);
        Windows::Data::Json::JsonObject^ GenerateTrackingJsonPayload(_In_ Platform::String^ eventName, _In_ Windows::Foundation::Collections::IPropertySet^ properties);


    private:
        ::Platform::String^ m_token;
    };

    unsigned WindowsTickToUnixSeconds(long long windowsTicks);
} } }
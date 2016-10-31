#pragma once

namespace CodevoidN { namespace Utilities { namespace Mixpanel {
    public ref class MixpanelClient sealed
    {
    public:
        MixpanelClient();
        void Track(_In_ ::Platform::String^ name, _In_ Windows::Foundation::Collections::IPropertySet^ properties);

    internal:
        Windows::Data::Json::JsonObject^ GenerateJsonPayload(_In_ Platform::String^ eventName, _In_ Windows::Foundation::Collections::IPropertySet^ properties);
    };
} } }
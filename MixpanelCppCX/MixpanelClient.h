#pragma once

namespace CodevoidN { namespace Utilities { namespace Mixpanel {
    public ref class MixpanelClient sealed
    {
    public:
        MixpanelClient(_In_ ::Platform::String^ token);
        void Track(_In_ ::Platform::String^ name, _In_ Windows::Foundation::Collections::IPropertySet^ properties);

        [Windows::Foundation::Metadata::DefaultOverload]
        void SetSuperProperty(_In_ ::Platform::String^ name, _In_ ::Platform::String^ value);
        
        [Windows::Foundation::Metadata::OverloadAttribute(L"SetSuperPropertyAsDouble")]
        void SetSuperProperty(_In_ ::Platform::String^ name, _In_ double value);

        [Windows::Foundation::Metadata::OverloadAttribute(L"SetSuperPropertyAsBoolean")]
        void SetSuperProperty(_In_ ::Platform::String^ name, _In_ bool value);

        ::Platform::String^ GetSuperProperty(_In_ ::Platform::String^ name);
        double GetSuperPropertyAsDouble(_In_ ::Platform::String^ name);
        bool GetSuperPropertyAsBool(_In_ ::Platform::String^ name);

        bool HasSuperProperty(_In_ ::Platform::String^ name);
        void ClearSuperProperties();

    internal:
        static void AppendPropertySetToJsonPayload(_In_ Windows::Foundation::Collections::IPropertySet^ properties, _In_ Windows::Data::Json::JsonObject^ toAppendTo);
        Windows::Data::Json::JsonObject^ GenerateTrackingJsonPayload(_In_ Platform::String^ eventName, _In_ Windows::Foundation::Collections::IPropertySet^ properties);


    private:
        void InitializeSuperPropertyCollection();

        ::Platform::String^ m_token;
        Windows::Foundation::Collections::IPropertySet^ m_superProperties;
    };

    unsigned WindowsTickToUnixSeconds(long long windowsTicks);
} } }
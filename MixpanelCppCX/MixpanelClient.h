#pragma once

namespace CodevoidN { namespace Utilities { namespace Mixpanel {
    /// <summary>
    /// MixpanelClient offers a API for interacting with Mixpanel for UWP apps running on Windows 10+
    /// </summary>
    public ref class MixpanelClient sealed
    {
    public:
        /// <summary>
        /// Constructs a new Mixpanel Client object to tracking usage
        /// <param name="token">The Mixpanel API Token to be used for this object</param>
        /// </summary>
        MixpanelClient(_In_ Platform::String^ token);

        /// <summary>
        /// Logs a datapoint to the Mixpanel Service with the supplied event name, and property set
        /// 
        /// <param name="name">The event name for the tracking call</param>
        /// <param name="properties">
        /// A value type only list of parameters to attach to this event.
        /// Note, none of these properties can be prefixed with "mp_". If they are, an exception will be thrown.
        /// For details on what can be used in these properties, see: https://mixpanel.com/help/reference/http
        /// </param>
        /// </summary>
        void Track(_In_ Platform::String^ name, _In_ Windows::Foundation::Collections::IPropertySet^ properties);

        /// <summary>
        /// Sets a property &amp; it's value that will be attached to all datapoints logged with
        /// this instance of the client. Suppports String, Double, and Boolean values.
        ///
        /// <param name="name">Name of the super property to set</param>
        /// <param name="value">Value to set for the super property</param>
        /// </summary>
        [Windows::Foundation::Metadata::DefaultOverload]
        void SetSuperProperty(_In_ Platform::String^ name, _In_ Platform::String^ value);
        
        [Windows::Foundation::Metadata::OverloadAttribute(L"SetSuperPropertyAsDouble")]
        void SetSuperProperty(_In_ Platform::String^ name, _In_ double value);

        [Windows::Foundation::Metadata::OverloadAttribute(L"SetSuperPropertyAsBoolean")]
        void SetSuperProperty(_In_ Platform::String^ name, _In_ bool value);

        /// <summary>
        /// Reads a currently set super property from any set super properties, and returns it as a String.
        ///
        /// If there is no super property with the requested name, an exception will be thrown. If the
        /// data type of the super property is not a String, an InvalidCastException will be thrown
        /// <param name="name">Name of the super property to read</param>
        /// </summary>
        Platform::String^ GetSuperPropertyAsString(_In_ Platform::String^ name);

        /// <summary>
        /// Reads a currently set super property from any set super properties, and returns it as a Double.
        ///
        /// If there is no super property with the requested name, an exception will be thrown. If the
        /// data type of the super property is not a Double, an InvalidCastException will be thrown
        /// <param name="name">Name of the super property to read</param>
        /// </summary>
        double GetSuperPropertyAsDouble(_In_ Platform::String^ name);

        /// <summary>
        /// Reads a currently set super property from any set super properties, and returns it as a Boolean.
        ///
        /// If there is no super property with the requested name, an exception will be thrown. If the
        /// data type of the super property is not a Boolean, an InvalidCastException will be thrown
        /// <param name="name">Name of the super property to read</param>
        /// </summary>
        bool GetSuperPropertyAsBool(_In_ Platform::String^ name);

        /// <summary>
        /// Checks if the super property has been set. Primarily intended to allow people to avoid
        /// exceptions when reading for a super property that hasn't been set yet.
        ///
        /// <param name="name">Super Property to check for being present</param>
        /// </summary>
        bool HasSuperProperty(_In_ Platform::String^ name);
        
        /// <summary>
        /// Clears any super propreties that might be present
        /// </summary>
        void ClearSuperProperties();

    internal:
        static void AppendPropertySetToJsonPayload(_In_ Windows::Foundation::Collections::IPropertySet^ properties, _In_ Windows::Data::Json::JsonObject^ toAppendTo);
        Windows::Data::Json::JsonObject^ GenerateTrackingJsonPayload(_In_ Platform::String^ eventName, _In_ Windows::Foundation::Collections::IPropertySet^ properties);
        property bool PersistSuperPropertiesToApplicationData;

    private:
        void InitializeSuperPropertyCollection();

        Platform::String^ m_token;
        Windows::Foundation::Collections::IPropertySet^ m_superProperties;
    };

    unsigned WindowsTickToUnixSeconds(long long windowsTicks);
} } }
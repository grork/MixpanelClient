#pragma once

namespace CodevoidN { namespace Utilities { namespace Mixpanel {
	/// <summary>
	/// The priority to be used when sending track events
	/// </summary>
	public enum class TrackSendPriority
	{
		/// <summary>
		/// The item is placed in a queue to be sent at some system
		/// managed point in the future
		/// </summary>
		Queue,

		/// <summary>
		/// The item is sent immediately to the service, and is not
		/// placed in the queue -- even if it fails to send successfully.
		/// </summary>
		Immediately
	};

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
        /// Logs a datapoint to the Mixpanel Service with the supplied event name, and property set.
		///
        /// These items are queued to be sent at a later time. If you wish to send them immediately
		/// <see cref="Track(String^,IPropertySet^,TrackSendPriority)" /> for more information
        /// <param name="name">The event name for the tracking call</param>
        /// <param name="properties">
        /// A value type only list of parameters to attach to this event.
        /// Note, none of these properties can be prefixed with "mp_". If they are, an exception will be thrown.
        /// For details on what can be used in these properties, see: https://mixpanel.com/help/reference/http
        /// </param>
        /// </summary>
		[Windows::Foundation::Metadata::DefaultOverload]
        void Track(_In_ Platform::String^ name, _In_ Windows::Foundation::Collections::IPropertySet^ properties);

		/// <summary>
		/// Logs a datapoint to the Mixpanel Service with the supplied event name, and property set.
		///
		/// The async operation returned will complete when the item has been queued, or when it has
		/// been sent to the service, depending on the value in the <paramref name="sendPriority" />
		/// parameter.
		///
		/// <param name="name">The event name for the tracking call</param>
		/// <param name="properties">
		/// A value type only list of parameters to attach to this event.
		/// Note, none of these properties can be prefixed with "mp_". If they are, an exception will be thrown.
		/// For details on what can be used in these properties, see: https://mixpanel.com/help/reference/http
		/// </param>
		/// <param name="sendPriority">The time frame in which to send this datapoint</param>
		/// </summary>
		[Windows::Foundation::Metadata::OverloadAttribute(L"TrackWithSendPriority")]
		Windows::Foundation::IAsyncAction^ Track(_In_ Platform::String^ name,
				            _In_ Windows::Foundation::Collections::IPropertySet^ properties,
			                _In_ TrackSendPriority sendPriority);

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

        /// <summary>
        /// Enables control of automatic attachment of the time to outgoing events.
        /// This is enabled by default, and places them in the track events at the point they are logged.
        ///
        /// If this is disabled, then the events will be assigned a time at the point at which they reach
        /// the service.
        /// </summary>
        property bool AutomaticallyAttachTimeToEvents;

    internal:
        static void AppendPropertySetToJsonPayload(_In_ Windows::Foundation::Collections::IPropertySet^ properties, _In_ Windows::Data::Json::JsonObject^ toAppendTo);
        Windows::Data::Json::JsonObject^ GenerateTrackingJsonPayload(_In_ Platform::String^ eventName, _In_ Windows::Foundation::Collections::IPropertySet^ properties);
        property bool PersistSuperPropertiesToApplicationData;

    private:
        void InitializeSuperPropertyCollection();
        concurrency::task<bool> PostTrackEventsToMixpanel(_In_ const std::vector<Windows::Data::Json::IJsonValue^>& payload, _In_ TrackSendPriority priority);

        Platform::String^ m_token;
        Windows::Foundation::Collections::IPropertySet^ m_superProperties;
    };

    unsigned WindowsTickToUnixSeconds(long long windowsTicks);
} } }
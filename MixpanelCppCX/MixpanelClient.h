#pragma once
#include "EventStorageQueue.h"

namespace CodevoidN { namespace Tests { namespace Mixpanel {
    class MixpanelTests;
} } }

namespace CodevoidN { namespace Utilities { namespace Mixpanel {
    /// <summary>
    /// MixpanelClient offers a API for interacting with Mixpanel for UWP apps running on Windows 10+
    /// </summary>
    public ref class MixpanelClient sealed
    {
        friend class CodevoidN::Tests::Mixpanel::MixpanelTests;

    public:
        /// <summary>
        /// Constructs a new Mixpanel Client object to tracking usage
        /// <param name="token">The Mixpanel API Token to be used for this object</param>
        /// </summary>
        MixpanelClient(Platform::String^ token);

        /// <summary>
        /// Initializes the client to be able to queue items to storage for
        /// resiliancy. Must be called before tracking an event
        /// </summary>
        Windows::Foundation::IAsyncAction^ InitializeAsync();

        /// <summary>
        /// Logs a datapoint to the Mixpanel Service with the supplied event name, and property set.
		///
        /// These items are queued to be sent at a later time, based on connecivity, queue length, and
		/// other ambient conditions.
        /// <param name="name">The event name for the tracking call</param>
        /// <param name="properties">
        /// A value type only list of parameters to attach to this event.
        /// Note, none of these properties can be prefixed with "mp_". If they are, an exception will be thrown.
        /// For details on what can be used in these properties, see: https://mixpanel.com/help/reference/http
        /// </param>
        /// </summary>
        void Track(Platform::String^ name, Windows::Foundation::Collections::IPropertySet^ properties);

        /// <summary>
        /// Sets a property &amp; it's value that will be attached to all datapoints logged with
        /// this instance of the client. Suppports String, Double, and Boolean values.
        ///
        /// <param name="name">Name of the super property to set</param>
        /// <param name="value">Value to set for the super property</param>
        /// </summary>
        [Windows::Foundation::Metadata::DefaultOverload]
        void SetSuperProperty(Platform::String^ name, Platform::String^ value);
        
        [Windows::Foundation::Metadata::OverloadAttribute(L"SetSuperPropertyAsDouble")]
        void SetSuperProperty(Platform::String^ name, double value);

        [Windows::Foundation::Metadata::OverloadAttribute(L"SetSuperPropertyAsBoolean")]
        void SetSuperProperty(Platform::String^ name, bool value);

        /// <summary>
        /// Reads a currently set super property from any set super properties, and returns it as a String.
        ///
        /// If there is no super property with the requested name, an exception will be thrown. If the
        /// data type of the super property is not a String, an InvalidCastException will be thrown
        /// <param name="name">Name of the super property to read</param>
        /// </summary>
        Platform::String^ GetSuperPropertyAsString(Platform::String^ name);

        /// <summary>
        /// Reads a currently set super property from any set super properties, and returns it as a Double.
        ///
        /// If there is no super property with the requested name, an exception will be thrown. If the
        /// data type of the super property is not a Double, an InvalidCastException will be thrown
        /// <param name="name">Name of the super property to read</param>
        /// </summary>
        double GetSuperPropertyAsDouble(Platform::String^ name);

        /// <summary>
        /// Reads a currently set super property from any set super properties, and returns it as a Boolean.
        ///
        /// If there is no super property with the requested name, an exception will be thrown. If the
        /// data type of the super property is not a Boolean, an InvalidCastException will be thrown
        /// <param name="name">Name of the super property to read</param>
        /// </summary>
        bool GetSuperPropertyAsBool(Platform::String^ name);

        /// <summary>
        /// Checks if the super property has been set. Primarily intended to allow people to avoid
        /// exceptions when reading for a super property that hasn't been set yet.
        ///
        /// <param name="name">Super Property to check for being present</param>
        /// </summary>
        bool HasSuperProperty(Platform::String^ name);
        
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

		/// <summary>
		/// Begins processing any events that get queued -- either currently, or in the future.s
		/// </summary>
		void Start();

		/// <summary>
		/// Stops processing items for uploading, and persists anything in memory to storage.
		/// It will return when the items have been persisted to disk.
		/// </summary>
		Windows::Foundation::IAsyncAction^ Pause();

		/// <summary>
		/// Removes any items that have been persisted to storage. e.g. If the user
		/// signs out, clear anything pending upload.
		/// </summary>
		Windows::Foundation::IAsyncAction^ ClearStorageAsync();

    private:
		/// <summary>
		/// Allows synchronous initalization if one has the storage
		/// folder to queue all the items to.
		///
		/// Primary intended use is for testing
		/// </summary>
		void Initialize(
			Windows::Storage::StorageFolder^ queueFolder,
            Windows::Foundation::Uri^ serviceUri
		);

		/// <summary>
		///	Configures the class for simpler / faster testing by:
		/// * Turning off writing to disk
		/// * Allows explicitly setting the variout timeouts & thresholds for the workers
		/// </summary>
		void ConfigureForTesting(const std::chrono::milliseconds& idleTimeout, const size_t& itemThreshold);

		/// <summary>
		/// For every rule, there is an exception. This enables some tests to
		/// actually write to disk.
		/// </summary>
		void ForceWritingToStorage();

		/// <summary>
		/// Allows the queue to be shutdown cleanly for testing purposes
		/// </summary>
		concurrency::task<void> Shutdown();

		/// <summary>
		/// By default all the super properties are persisted to storage.
		/// For testing, we don't want to do that. Settings this flag
		/// disables that option, and keeps everything in memory for the
		/// lifetime of this instance.
		/// </summary>
		property bool PersistSuperPropertiesToApplicationData;

        /// <summary>
        /// Intended to initalize the worker queues (but not start them), primarily
        /// due to the need to open / create the queue folder if neededed.
        /// </summary>
        concurrency::task<void> Initialize();

		/// <summary>
		/// Sends the supplied JSON items to the service as a batch.
		/// The returned task will be completed when all the supplied items
		/// have been sent to the service.
		/// </summary>
        concurrency::task<bool> PostTrackEventsToMixpanel(const std::vector<Windows::Data::Json::IJsonValue^>& payload);

		static concurrency::task<bool> SendRequestToService(Windows::Foundation::Uri^ uri,
													 Windows::Foundation::Collections::IMap<Platform::String^, Windows::Data::Json::IJsonValue^>^ payload,
													 Windows::Web::Http::Headers::HttpProductInfoHeaderValue^ userAgent);
		
		// Helpers to testing upload logic
        void SetUploadToServiceMock(const std::function<concurrency::task<bool>(
            Windows::Foundation::Uri^,
            Windows::Foundation::Collections::IMap<Platform::String^, Windows::Data::Json::IJsonValue^>^,
            Windows::Web::Http::Headers::HttpProductInfoHeaderValue^
        )> mock);

		// Helpers for testing the persist to storage behaviour
        void SetWrittenToStorageMock(const std::function<void(std::vector<std::shared_ptr<CodevoidN::Utilities::Mixpanel::PayloadContainer>>)> mock);
        std::function<void(const std::vector<std::shared_ptr<CodevoidN::Utilities::Mixpanel::PayloadContainer>>)> m_writtenToStorageMockCallback;

		std::vector<std::shared_ptr<CodevoidN::Utilities::Mixpanel::PayloadContainer>>
			HandleEventBatchUpload(
				const std::vector<std::shared_ptr<CodevoidN::Utilities::Mixpanel::PayloadContainer>>& items,
				const std::function<bool()>& shouldKeepProcessing
			);
        void HandleCompletedUploads(const std::vector<std::shared_ptr<CodevoidN::Utilities::Mixpanel::PayloadContainer>>& items);
		void AddItemsToUploadQueue(const std::vector<std::shared_ptr<CodevoidN::Utilities::Mixpanel::PayloadContainer>>& items);
		Windows::Data::Json::JsonObject^ GenerateTrackingJsonPayload(Platform::String^ eventName, Windows::Foundation::Collections::IPropertySet^ properties);
		static void AppendPropertySetToJsonPayload(Windows::Foundation::Collections::IPropertySet^ properties, Windows::Data::Json::JsonObject^ toAppendTo);
		void ThrowIfNotInitialized();
		void InitializeSuperPropertyCollection();

		/// <summary>
		/// The API Token being used for all requests
		/// </summary>
        Platform::String^ m_token;
        Windows::Foundation::Collections::IPropertySet^ m_superProperties;

        Windows::Foundation::Uri^ m_serviceUri;
		Windows::Web::Http::Headers::HttpProductInfoHeaderValue^ m_userAgent;
        std::unique_ptr<CodevoidN::Utilities::Mixpanel::EventStorageQueue> m_eventStorageQueue;
		CodevoidN::Utilities::BackgroundWorker<CodevoidN::Utilities::Mixpanel::PayloadContainer> m_uploadWorker;
		std::function<concurrency::task<bool>(
			Windows::Foundation::Uri^,
			Windows::Foundation::Collections::IMap<Platform::String^, Windows::Data::Json::IJsonValue^>^,
			Windows::Web::Http::Headers::HttpProductInfoHeaderValue^)> m_requestHelper;
    };

    unsigned WindowsTickToUnixSeconds(long long windowsTicks);
} } }
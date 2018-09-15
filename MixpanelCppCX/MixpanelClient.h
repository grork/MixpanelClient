#pragma once
#include "DurationTracker.h"
#include "EventStorageQueue.h"

namespace Codevoid::Tests::Mixpanel {
    class MixpanelTests;
}

namespace Codevoid::Utilities::Mixpanel {

    enum EngageOperationType
    {
        Set,
        Set_Once,
        Add,
        Append,
        Union,
        Remove,
        Unset,
        DeleteProfile,
    };

    /// <summary>
    /// Represents the different type of updates that can be performed on
    /// profile that has been created on the service. See
    /// https://mixpanel.com/help/reference/http for more details on each
    /// of these, and what they may mean for your profile.
    /// </summary>
    public enum class UserProfileOperation {
        /// <summary>
        /// Sets the properties to the values provided, overwriting any
        /// that might already exist. If they don't exist, they're created
        /// </summary>
        Set,

        /// <summary>
        /// If a property already exists, then the value will not be updated.
        /// If it doesn't, it will be created.
        /// </summary>
        Set_Once,

        /// <summary>
        /// Adds the numerical values of a property to an existing value, and
        /// saves the result in that property. If the proeprty doesn't exist
        /// then it the values provided are added to 0.
        /// </summary>
        Add,

        /// <summary>
        /// Assumes that the values are sets, and appends the set to any
        /// existing values that might be on the service.
        /// </summary>
        Append,

        /// <summary>
        /// Assumes the values are sets, and merges the set to any existing
        /// values that might be on the service. If an value in the set is already
        /// there, it is not duplicated.
        /// </summary>
        Union,

        /// <summary>
        /// Assumes values are sets, and removes any items in the set from the
        /// values that might be on the service.
        /// </summary>
        Remove,

        /// <summary>
        /// Removes the entire property from the users profile, like it had
        /// never been there.
        /// </summary>
        Unset
    };

    /// <summary>
    /// MixpanelClient offers a API for interacting with Mixpanel for UWP apps running on Windows 10+
    /// </summary>
    public ref class MixpanelClient sealed
    {
        friend class Codevoid::Tests::Mixpanel::MixpanelTests;

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
        /// Begins tracking the duration of the named event. When an event is tracked
        /// with the same name by calling Track, a "duration" property will be added
        /// to the event payload with the since starting the timer.
        /// </summary>
        void StartTimedEvent(Platform::String^ name);

        /// <summary>
        /// Explicitly starts a restarts session. If there is one already in progress, the
        /// in progress session will be ended, otherwise starts one.Intended to be used in
        /// situations where your session doesn't always match between suspend &amp; resume.
        //  E.g.you want to start a session when someone logs in.
        /// </summary>
        void RestartSessionTracking();

        /// <summary>
        /// Sets a string property &amp; it's value that will be attached to the session event
        /// logged with the current session.
        ///
        /// <param name="name">Name of the session property to set</param>
        /// <param name="value">Value to set for the session property</param>
        /// </summary>
        void SetSessionPropertyAsString(Platform::String^ name, Platform::String^ value);

        /// <summary>
        /// Sets a integer property &amp; it's value that will be attached to the session event
        /// logged with the current session.
        ///
        /// <param name="name">Name of the session property to set</param>
        /// <param name="value">Value to set for the session property</param>
        /// </summary>
        void SetSessionPropertyAsInteger(Platform::String^ name, int value);

        /// <summary>
        /// Sets a double property &amp; it's value that will be attached to the session event
        /// logged with the current session.
        ///
        /// <param name="name">Name of the session property to set</param>
        /// <param name="value">Value to set for the session property</param>
        /// </summary>
        void SetSessionPropertyAsDouble(Platform::String^ name, double value);

        /// <summary>
        /// Sets a boolean property &amp; it's value that will be attached to the session event
        /// logged with the current session.
        ///
        /// <param name="name">Name of the session property to set</param>
        /// <param name="value">Value to set for the session property</param>
        /// </summary>
        void SetSessionPropertyAsBoolean(Platform::String^ name, bool value);

        /// <summary>
        /// Removes a property &amp; it's value from the session properties associated with
        /// the current ession.
        /// </summary>
        void RemoveSessionProperty(Platform::String^ name);

        /// <summary>
        /// Reads a currently set session property from any set session properties, and returns it as a String.
        ///
        /// If there is no session property with the requested name, an exception will be thrown. If the
        /// data type of the session property is not a String, an InvalidCastException will be thrown
        /// <param name="name">Name of the session property to read</param>
        /// </summary>
        Platform::String^ GetSessionPropertyAsString(Platform::String^ name);

        /// <summary>
        /// Reads a currently set session property from any set session properties, and returns it as a Integer.
        ///
        /// If there is no session property with the requested name, an exception will be thrown. If the
        /// data type of the session property is not a Integer, an InvalidCastException will be thrown
        /// <param name="name">Name of the session property to read</param>
        /// </summary>
        int GetSessionPropertyAsInteger(Platform::String^ name);

        /// <summary>
        /// Reads a currently set session property from any set session properties, and returns it as a Double.
        ///
        /// If there is no session property with the requested name, an exception will be thrown. If the
        /// data type of the session property is not a Double, an InvalidCastException will be thrown
        /// <param name="name">Name of the session property to read</param>
        /// </summary>
        double GetSessionPropertyAsDouble(Platform::String^ name);

        /// <summary>
        /// Reads a currently set session property from any set session properties, and returns it as a Boolean.
        ///
        /// If there is no session property with the requested name, an exception will be thrown. If the
        /// data type of the session property is not a Boolean, an InvalidCastException will be thrown
        /// <param name="name">Name of the session property to read</param>
        /// </summary>
        bool GetSessionPropertyAsBool(Platform::String^ name);

        /// <summary>
        /// Checks if the session property has been set. Primarily intended to allow people to avoid
        /// exceptions when reading for a session property that hasn't been set yet.
        ///
        /// <param name="name">session Property to check for being present</param>
        /// </summary>
        bool HasSessionProperty(Platform::String^ name);

        /// <summary>
        /// Clears any session propreties that might be present
        /// </summary>
        void ClearSessionProperties();

        /// <summary>
        /// Sets a string property &amp; it's value that will be attached to all datapoints logged
        /// with this instance of the client. Suppports String, Double, and Boolean values.
        ///
        /// <param name="name">Name of the super property to set</param>
        /// <param name="value">Value to set for the super property</param>
        /// </summary>
        void SetSuperPropertyAsString(Platform::String^ name, Platform::String^ value);

        /// <summary>
        /// Sets a integer property &amp; it's value that will be attached to all datapoints logged
        /// with this instance of the client. Suppports String, Double, and Boolean values.
        ///
        /// <param name="name">Name of the super property to set</param>
        /// <param name="value">Value to set for the super property</param>
        /// </summary>
        void SetSuperPropertyAsInteger(Platform::String^ name, int value);

        /// <summary>
        /// Sets a double property &amp; it's value that will be attached to all datapoints logged
        /// with this instance of the client. Suppports String, Double, and Boolean values.
        ///
        /// <param name="name">Name of the super property to set</param>
        /// <param name="value">Value to set for the super property</param>
        /// </summary>
        void SetSuperPropertyAsDouble(Platform::String^ name, double value);
        
        /// <summary>
        /// Sets a boolean property &amp; it's value that will be attached to all datapoints logged
        /// with this instance of the client. Suppports String, Double, and Boolean values.
        ///
        /// <param name="name">Name of the super property to set</param>
        /// <param name="value">Value to set for the super property</param>
        /// </summary>
        void SetSuperPropertyAsBoolean(Platform::String^ name, bool value);

        /// <summary>
        /// Removes a property &amp; it's value from the super properties associated with
        /// this instance of the client.
        /// </summary>
        void RemoveSuperProperty(Platform::String^ name);

        /// <summary>
        /// Reads a currently set super property from any set super properties, and returns it as a String.
        ///
        /// If there is no super property with the requested name, an exception will be thrown. If the
        /// data type of the super property is not a String, an InvalidCastException will be thrown
        /// <param name="name">Name of the super property to read</param>
        /// </summary>
        Platform::String^ GetSuperPropertyAsString(Platform::String^ name);

        /// <summary>
        /// Reads a currently set super property from any set super properties, and returns it as a Integer.
        ///
        /// If there is no super property with the requested name, an exception will be thrown. If the
        /// data type of the super property is not a Integer, an InvalidCastException will be thrown
        /// <param name="name">Name of the super property to read</param>
        /// </summary>
        int GetSuperPropertyAsInteger(Platform::String^ name);

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
        /// When enabled, tracks the duration of sessions automatically, based on
        /// suspend &amp; resume events.
        /// </summary>
        property bool AutomaticallyTrackSessions;

        /// <summary>
        /// When enabled, and even is tracked, it is immediately dropped, and never
        /// sent anywhere. Intent is to allow clients to provide privacy options
        /// to their user, but maintain all the rest of their tracking logic.
        /// </summary>
        property bool DropEventsForPrivacy;

        /// <summary>
        /// Begins processing any events that get queued -- either currently, or in the future.s
        /// </summary>
        void Start();

        /// <summary>
        /// Stops processing items for uploading, and persists anything in memory to storage.
        /// It will return when the items have been persisted to disk.
        /// </summary>
        Windows::Foundation::IAsyncAction^ PauseAsync();

        /// <summary>
        /// Removes any items that have been persisted to storage. e.g. If the user
        /// signs out, clear anything pending upload.
        /// </summary>
        Windows::Foundation::IAsyncAction^ ClearStorageAsync();

        /// <summary>
        /// If a user ID had been set via SetUserIdentityExplicitly, or generating
        /// one, this will return true. Intended to be checked before setting an identity.
        /// </summary>
        bool HasUserIdentity();

        /// <summary>
        /// Sets the value of 'distinct_id' property of your events when tracked to ensure that
        /// they can be tracked as a user. With this, it'll always be attached to all your requests.
        /// </summary>
        void SetUserIdentityExplicitly(Platform::String^ identity);

        /// <summary>
        /// When called, the user identity set, if any, will be cleared and your subsequent events
        /// will be individual events with no ability to track over time the behaviour of one client.
        /// </summary>
        void ClearUserIdentity();

        /// <summary>
        /// Recommended way to set a user identity -- this generates a GUID for the user identity
        /// and sets it to be attached on all future events.
        /// </summary>
        void GenerateAndSetUserIdentity();

        /// <summary>
        /// Updates the profile for the user identity in this instance, with the properties provided.
        /// See https://mixpanel.com/help/reference/http for details on the various operations behaviour.
        /// </summary>
        void UpdateProfile(UserProfileOperation operation, Windows::Foundation::Collections::IPropertySet^ properties);

        /// <summary>
        /// Updates the profile for the user identity in this instance, with the properties provided.
        /// See https://mixpanel.com/help/reference/http for details on the various operations behaviour.
        /// </summary>
        void UpdateProfileWithOptions(UserProfileOperation operation,
                                      Windows::Foundation::Collections::IPropertySet^ properties,
                                      Windows::Foundation::Collections::IPropertySet^ options);

        /// <summary>
        /// Deletes the profile for the current user identity, and clears any locally set identity
        /// </summary>
        void DeleteProfile();

    private:
        /// <summary>
        /// Allows synchronous initalization if one has the storage
        /// folder to queue all the items to.
        ///
        /// Primary intended use is for testing
        /// </summary>
        void Initialize(
            Windows::Storage::StorageFolder^ trackQueueFolder,
            Windows::Storage::StorageFolder^ profileQueueFolder,
            Windows::Foundation::Uri^ serviceUri
        );

        /// <summary>
        /// Starts tracking a session (e.g. the duration)
        /// </summary>
        void StartSessionTracking();

        /// <summary>
        /// Ends the session, and sends any session properties + duration of
        /// the session into the queue to be uploaded.
        /// </summary>
        void EndSessionTracking();

        /// <summary>
        ///	Configures the class for simpler / faster testing by:
        /// * Turning off writing to disk
        /// * Allows explicitly setting the variout timeouts &amp; thresholds for the workers
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
        /// Start worker that does the work of starting the queues, but doesn't do some of the
        /// one-time work related to starting the queue (E.g. attacking handlers)
        /// </summary>
        void StartWorkers();

        /// <summary>
        /// Projects the internal task-based interface for pausing, rather than
        /// asking internal consumers to consume IAsyncAction^ interface.
        /// </summary>
        concurrency::task<void> PauseWorkers();

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
        concurrency::task<bool> PostPayloadToUri(Windows::Foundation::Uri^ destination, const std::vector<Windows::Data::Json::IJsonValue^>& payload);

        /// <summary>
        /// Handles suspending event as raised from the platform's CoreApplication object
        /// Intended to shutdown &amp; clean up any events that are not on disk, and stop the
        /// upload queue whereever it is.
        /// </summary>
        void HandleApplicationSuspend(Platform::Object^ sender, Windows::ApplicationModel::SuspendingEventArgs^ args);

        /// <summary>
        /// Handles resuming from the platform; just starts up the workers again. Should return
        /// quickly so the UI doesn't hang.
        /// </summary>
        void HandleApplicationResuming(Platform::Object^ sender, Platform::Object^ args);

        /// <summary>
        /// Handles the app entering the background, intended to inform the automatic duration
        /// of events that it should stop measuring the time.
        /// </summary>
        void HandleApplicationEnteredBackground(Platform::Object^ sender, Windows::ApplicationModel::EnteredBackgroundEventArgs^ args);

        /// <summary>
        /// Handles the app leaving the background, intended to inform the automatic duration
        /// of events that it should now start measuring the time again.
        /// </summary>
        void HandleApplicationLeavingBackground(Platform::Object^ sender, Windows::ApplicationModel::LeavingBackgroundEventArgs^ args);

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
        void SetTrackWrittenToStorageMock(const std::function<void(std::vector<std::shared_ptr<Codevoid::Utilities::Mixpanel::PayloadContainer>>)> mock);
        std::function<void(const std::vector<std::shared_ptr<Codevoid::Utilities::Mixpanel::PayloadContainer>>)> m_trackWrittenToStorageMockCallback;

        void SetProfileWrittenToStorageMock(const std::function<void(std::vector<std::shared_ptr<Codevoid::Utilities::Mixpanel::PayloadContainer>>)> mock);
        std::function<void(const std::vector<std::shared_ptr<Codevoid::Utilities::Mixpanel::PayloadContainer>>)> m_profileWrittenToStorageMockCallback;

        std::vector<std::shared_ptr<Codevoid::Utilities::Mixpanel::PayloadContainer>>
            HandleBatchUploadWithUri(
                Windows::Foundation::Uri^ destination,
                const std::vector<std::shared_ptr<Codevoid::Utilities::Mixpanel::PayloadContainer>>& items,
                const std::function<bool()>& shouldKeepProcessing
            );
        static void HandleCompletedUploadsForQueue(Codevoid::Utilities::Mixpanel::EventStorageQueue& queue, const std::vector<std::shared_ptr<Codevoid::Utilities::Mixpanel::PayloadContainer>>& items);
        static Windows::Data::Json::JsonObject^ GenerateTrackJsonPayload(Platform::String^ eventName, Windows::Foundation::Collections::IPropertySet^ properties);
        static Windows::Data::Json::JsonObject^ GenerateEngageJsonPayload(EngageOperationType operation, Windows::Foundation::Collections::IPropertySet^ values, Windows::Foundation::Collections::IPropertySet^ options);
        Windows::Foundation::Collections::IPropertySet^ EmbelishPropertySetForTrack(Windows::Foundation::Collections::IPropertySet^ properties);
        Windows::Foundation::Collections::IPropertySet^ GetEngageProperties(Windows::Foundation::Collections::IPropertySet^ options);
        void AddDurationForTrack(Platform::String^, Windows::Foundation::Collections::IPropertySet^ properties);
        static void AppendPropertySetToJsonPayload(Windows::Foundation::Collections::IPropertySet^ properties, Windows::Data::Json::JsonObject^ toAppendTo);
        static void AppendNumericPropertySetToJsonPayload(Windows::Foundation::Collections::IPropertySet^ properties, Windows::Data::Json::JsonObject^ toAppendTo);
        void ThrowIfNotInitialized();
        Windows::Foundation::Collections::IPropertySet^ InitializeSuperPropertyCollection();
        Windows::Foundation::Collections::IPropertySet^ InitializeSessionPropertyCollection();
        Platform::String^ GetDistinctId();

        /// <summary>
        /// The API Token being used for all requests
        /// </summary>
        Platform::String^ m_token;
        Windows::Foundation::Collections::IPropertySet^ m_superProperties;
        Windows::Foundation::Collections::IPropertySet^ m_sessionProperties;

        Codevoid::Utilities::Mixpanel::DurationTracker m_durationTracker;
        Windows::Foundation::Uri^ m_trackEventUri;
        Windows::Foundation::Uri^ m_engageUri;
        Windows::Web::Http::Headers::HttpProductInfoHeaderValue^ m_userAgent;
        std::unique_ptr<Codevoid::Utilities::Mixpanel::EventStorageQueue> m_trackStorageQueue;
        std::unique_ptr<Codevoid::Utilities::Mixpanel::EventStorageQueue> m_profileStorageQueue;
        Codevoid::Utilities::BackgroundWorker<Codevoid::Utilities::Mixpanel::PayloadContainer> m_trackUploadWorker;
        Codevoid::Utilities::BackgroundWorker<Codevoid::Utilities::Mixpanel::PayloadContainer> m_profileUploadWorker;
        std::function<concurrency::task<bool>(
            Windows::Foundation::Uri^,
            Windows::Foundation::Collections::IMap<Platform::String^, Windows::Data::Json::IJsonValue^>^,
            Windows::Web::Http::Headers::HttpProductInfoHeaderValue^)> m_requestHelper;
        Windows::Foundation::EventRegistrationToken m_suspendingEventToken;
        Windows::Foundation::EventRegistrationToken m_resumingEventToken;
        Windows::Foundation::EventRegistrationToken m_enteredBackgroundEventToken;
        Windows::Foundation::EventRegistrationToken m_leavingBackgroundEventToken;
        bool m_sessionTrackingStarted = false;
    };

    unsigned WindowsTickToUnixSeconds(long long windowsTicks);
}
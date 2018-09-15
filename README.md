MixpanelCppCX
=============

Introduction
------------
MixpanelCppCX is a client for the [Mixpanel](https://mixpanel.com) analytics
service. It has been built to handle the common situations of uploading events
to the service. It supports batching, persisting-to-storage-before-uploading,
and [super properties](https://mixpanel.com/help/reference/javascript#super-properties).

It also supports the [enagement/userprofile API](https://mixpanel.com/help/reference/http#people-analytics-updates).
The official documentation will provide the right detail on how to use this part
API.

It's intended to be consumed by any UWP app — but the original motivation was to
support JavaScript applications.

How to get it
-------------
MixpanelCppCX is published as a NuGET package
([`Codevoid.Utilities.Mixpanel`](https://nuget.org/packages/Codevoid.Utilities.Mixpanel/))
to the NuGET.org feed — just add it to your UWP Project in Visual Studio, and
it'll be available for use in your code.

Things to note
--------------
- Items awaiting to upload are stored in a folder in your applications private
data folder with in local settings. (`%LOCALAPPDATA%\Packages\<PackageIdentity>\LocalState\MixpanelUploadQueue`)
- Items awaiting upload are written to disk after 500ms of idle time, or when
there are 10 or more items in the queue — whichever comes first.
- Items are uploaded in batches of 50, unless one item in the batch fails, then
they're uploaded 1 at a time.
- The queue is paused automatically when being suspended, and resumed when...
resumed.
- This library does automatic session tracking — it starts tracking the session
duration on start, and will automatically end/resume the session based on
suspend/resume events.

Usage
=====

Sending a simple event
----------------------
1. Create an instance of `Codevoid.Utilities.Mixpanel`, and hold on to it.
```
var mixpanelClient = new Codevoid.Utilities.Mixpanel(<Mixpanel API Token>)
```
2. Call, and wait for, `.InitializeAsync()`
3. Call `.Start()` on that instance to start processing the queue.
4. Call `.Track("MyEvent", null)` to log a single event

If you need to send properties with your event you can use `Windows.Foundation.Collections.PropertySet`,
and insert your properties as needed, passing it as the second parameter to
`Track`:
```
var properties = new Windows.Foundation.Colllections.PropertySet();
properties.add("MyProperty", "MyValue");
properties.add("MyOtherProperty", 9);

mixpanelClient.Track("MyOtherEvent", properties);
```

Setting a User Identity
-----------------------
If you want to be able to track your customers across multiple settings, and
correlate the events that a device/customer generates, you need to set an identity
for that user.

You can do that in two ways:
1. Call `GenerateAndSetUserIdentity` and create a GUID identifier
2. Use your own method for generating a unique identifier, and call `SetUserIdentityExplicitly`.

From that point, any events will have that identifier logged.

If you want to check if you have identified this user, you can call `HasUserIdentity`.

Adding a super property
-----------------------
Assuming you have an instance of `MixpanelClient`, you can set super properties
with three data types -- `String`, `Double`, and `Boolean`:
```
mixpanelClient.SetSuperPropertyAsString("PropertyName", "PropertyValue");
mixpanelClient.SetSuperPropertyAsDouble("DoublePropertyName", 3.14);
mixpanelClient.SetSuperPropertyAsBoolean("BooleanPropertyName", false);
```

Once set, the super properties will attached to every event when logged,
automatically. This is useful for tracking against a single user, for example.

Adding a session property
-----------------------
Assuming you have an instance of `MixpanelClient`, you can set session properties
with three data types -- `String`, `Double`, and `Boolean`:
```
mixpanelClient.SetSessionPropertyAsString("PropertyName", "PropertyValue");
mixpanelClient.SetSessionPropertyAsDouble("DoublePropertyName", 3.14);
mixpanelClient.SetSessionPropertyAsBoolean("BooleanPropertyName", false);
```

Once set, the session properties will attached to the session event when the
session ends. This is useful to tracking how often something happened in a session

Updating & Storing User Profiles
--------------------------------
Once you've set the identity of the user, you are able to update the infromation
that Mixpanel has about an identity -- this enables you to perform deeper analysis
of your customer behaviour.

To update & change a profile, you can use the `UpdateProfile` API, and passing
properties to update those values.

```
mixpanelClient.UpdateUserProfile(UserProfileOperation.Set, propertySet);
```


API
===

MixpanelClient(String token) (Constructor)
------------------------------------------
Creates an instance of the class, and uses the supplied token to make the
requests to Mixpanel.

```
var mixpanelClient = new Codevoid.Utilities.MixpanelClient("YOUR TOKEN HERE");
```

### Parameters
`token` — The token passed to the service with your events (required)

## _Properties_ ##
bool AutomaticallyAttachTimeToEvents
------------------------------------
By default, all events have a time attached to them to indicate the order of the
events, even if they're uploaded to the service out of order.

This property allows these to be disabled if you have your own way in which you
set the time.

If you wish to override the time for a single event, include a property named
"time" in the payload.

```
mixpanelClient.AutomaticallyAttachTimeToEvents = false;
```

bool AutomaticallyTrackSessions
------------------------------------
By default, session duration is tracked & sent to the service. The session is
started when Start() is called, and ends either when the app is suspended or
when RestartSession()/Shutdown() is called.

Set this to false to have the session tracking turned off.

```
mixpanelClient.AutomaticallyTrackSessions = false;
```

bool DropEventsForPrivacy
-------------------------

When enabled, and even is tracked, it is immediately dropped, and never uploaded
to the service. Intended to ube used in situations where the consumer offers a
choice to their users about turning off all telemetry logging. With this option
set, you can still call track, but _nothing_ happens - it's immediately dropped.

Note, if you have events queued at this point they'll continue to be processed.

```
mixpanelClient.DropEventsForPrivacy = true;
```

## _Methods_ ##

IAsyncAction InitializeAsync()
------------------------------
Initializes the instance to be ready to process events, but does **not** start
processing them. This gets the queue folder locally, and loads any unsent items
from a previous session.

```
mixpanelClient.InitializeAsync().done(...);
```

bool HasUserIdentity()
----------------------
Checks if a user identity has been set, and if so, returns 'true'.

```
mixpanelClient.HasUserIdentity();
```

void GenerateAndSetUserIdentity()
---------------------------------
Generates a random identifier for a user, and sets it for all future events. Any
existing identity that may or may not be stored will be overwritten.

```
if(!mixpanelClient.HasUserIdentity()) {
    mixpanelClient.GenerateAndSetUserIdentity();
}
```

void SetUserIdentityExplicitl(String identity)
----------------------------------------------
Sets an explicit identifier for a user to be attached to all future events. Any
existing identity will be overwritten.

```
mixpanelClient.SetUserIdentityExplicitly("User1");
```

### Parameters
`identity` - The user Identity be be set for future events.

void Track(String name, IPropertySet properties)
------------------------------------------------
Adds the event with the supplied name & properties (in addition to any super
properties) to the upload queue. It is _not_ sent immediately, and there are no
promises/events to know when it's made it to the service.

```
mixpanelClient.Track("LoggedIn", null);
```

### Parameters
`name` — The name of the event that you are wishing to track

`properties` — The properties & values that are associated with this event.

void UpdateUserProfile(UserProfileOperation operation, IPropertySet properties)
-------------------------------------------------------------------------------
Updates a users profile with the properties provided, applying the operation
supplied. These follow the behaviour documented in Mixpanel's [documentation](https://mixpanel.com/help/reference/http#people-analytics-updates).

### Parameters
`operation` — The type operation to perform with the provided properties.

`properties` — The properties & values to apply with the the provided operation

##### UserProfileOperation
This enumeration maps to the specific operations that mixpanel [allows for user
profile operations](https://mixpanel.com/help/reference/http#people-analytics-updates).

`Set` —  Sets the properties to the values provided, overwriting any that might
already exist. If they don't exist, they're created.

`Set_Once` — If a property already exists, then the value will not be updated.
If it doesn't, it will be created.

`Add` — Adds the numerical values of a property to an existing value, and saves
the result in that property. If the proeprty doesn't exist then it the values
provided are added to 0.

`Append` — Assumes that the values are sets, and appends the set to any existing
values that might be on the service.

`Union` — Assumes the values are sets, and merges the set to any existing values
that might be on the service. If an value in the set is already there, it is not
duplicated.

`Remove` — Assumes values are sets, and removes any items in the set from the
values that might be on the service.

`Unset` — Removes the entire property from the users profile, like it had never
been there.

void ClearUserIdentity()
------------------------
Clears any user identity that might be set, so that future events are no longer
associated with a user.

This should be called after a `ClearSuperProperties` to truely return to a default
state.

void DeleteUserProfile()
------------------------
Asks Mixpanel to delete the information related to the current user identity.
It also clears any local user identity that is stored.

### Parameters
`name` — Name of the event that is being logged (required)

`properties` — Any additional properties you would like to log with this event (optional)

void StartTimedEvent(string name)
---------------------------------
Starts a timing the duration of an event. After this, if an event is tracked
through `Track` with the same name, a `duration` property will be attached to
the event when tracked.

```
mixpanelClient.StartTimedEvent("LoggedIn");
```

void RestartSessionTracking()
---------------------------------
Explicitly starts a restarts session. If there is one already in progress, the
in progress session will be ended, otherwise starts one. Intended to be used in
situations where your session doesn't always match between suspend & resume.
E.g. you want to start a session when someone logs in.

```
mixpanelClient.RestartSessionTracking();
```

### Parameters
`name` — Name of the event that the duration is to be tracked (required)

void SetSessionPropertyAs<Type>(String name, [String/Integer/Double/Boolean] value)
-----------------------------------------------------------------
Sets a session property on the class, that is persisted for the duration of the
session. You can store `string`'s, `integer`'s, `double`'s, and `boolean`'s.
When consuming this, you need to use the named versions of this method: 
`SetSessionPropertyAsString` for strings, `SetSessionPropertyAsIntegter` for
integers, `SetSessionPropertyAsDouble` for doubles, and
`SetSessionPropertyAsBoolean` for, obviously, `Booleans`.

```
mixpanelClient.SetSessionProperty("ASessionProperty", "SessionValue");
```

### Parameters
`name` — Name of the property that is to be set (required)
`value` — Value to be set for the supplied name (required)

string/double/bool GetSessionPropertyAsString/Integer/Double/Boolean(string name)
-----------------------------------------------------------------------
Retrieves a value from the session property storage, and returns it to the caller.

Since they're all different only in their return type, and accept the same
parameters, they're named differently based on their datatype.

```
var currentValue = mixpanelClient.GetSessionProperty("ASessionProperty");
```

### Parameters
`name` — Named session property to retrieve.

bool HasSessionProperty(string name)
----------------------------------
Checks if a session property has been set & has a value.

```
if(mixpanelClient.HasSessionProperty("ASessionProperty")) {
    console.log("ASessionProperty has been set");
}
```

`name` — Property to check for being set.

void RemoveSessionProperty(string name)
-------------------------------------
Removes a session property from the set of session properties. If the property isn't
set, then the method returns silently.

```
mixpanelClient.RemoveSessionProperty("ASessionProperty");
```

### Parameters
`name` — Property to remove.

void ClearSessionProperties()
---------------------------
Clears all currently set session properties.

```
mixpanelClient.ClearSessionProperties();
```

void SetSuperPropertyAs<Type>(String name, [String/Integer/Double/Boolean] value)
-----------------------------------------------------------------
Sets a super property on the class, that is persisted across instances. You can
store `string`'s, `int`'s,`double`'s, and `boolean`'s. When consuming this, you
need to use the named versions of this method:
`SetSuperPropertyAsString` for strings, `SetSuperPropertyAsInteger` for integers,
`SetSuperPropertyAsDouble` for doubles, and `SetSuperPropertyAsBoolean` for,
obviously, `Booleans`.

```
mixpanelClient.SetSuperProperty("ASuperProperty", "SuperValue");
```

### Parameters
`name` — Name of the property that is to be set (required)

`value` — Value to be set for the supplied name (required)

string/double/bool GetSuperPropertyAsString/Integer/Double/Boolean(string name)
-----------------------------------------------------------------------
Retrieves a value from the super property storage, and returns it to the caller.

Since they're all different only in their return type, and accept the same
parameters, they're named differently based on their datatype.

```
var currentValue = mixpanelClient.GetSuperProperty("ASuperProperty");
```

### Parameters
`name` — Named super property to retrieve.

bool HasSuperProperty(string name)
----------------------------------
Checks if a super property has been set & has a value.

```
if(mixpanelClient.HasSuperProperty("ASuperProperty")) {
    console.log("ASuperProperty has been set");
}
```

`name` — Property to check for being set.

void RemoveSuperProperty(string name)
-------------------------------------
Removes a super property from the set of super properties. If the property isn't
set, then the method returns silently.

```
mixpanelClient.RemoveSuperProperty("ASuperProperty");
```

### Parameters
`name` — Property to remove.

void ClearSuperProperties()
---------------------------
Clears all currently set super properties, excluding the user identity. To clear
that, call `ClearUserIdentity`.

```
mixpanelClient.ClearSuperProperties();
```

void Start()
------------
Starts processing any events that are queued.

```
mixpanelClient.Start();
```

IAsyncAction PauseAsync()
-------------------------
Drains the queue to storage, and stops processing any events pending upload to
the service.

Once you're ready to process events again, call `Start()`.

```
mixpanelClient.PauseAsync().done(...);
```

IAsyncAction ClearStorageAsync()
--------------------------------
Removes any events that have been queued to storage, but have not been uploaded
to the service yet.

```
mixpanelClient.ClearStorageAsync().done(...);
```
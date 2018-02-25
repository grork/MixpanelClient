MixpanelCppCX
=============

Introduction
------------
MixpanelCppCX is a client for the [Mixpanel](https://mixpanel.com) analytics
service. It has been built to handle the common situations of uploading events
to the service. It supports batching, persisting-to-storage-before-uploading,
and [super properties](https://mixpanel.com/help/reference/javascript#super-properties).
It's intended to be consumed by any UWP app — but the original motivation was to
support JavaScript applications.

How to get it
-------------
MixpanelCppCX is published as a NuGET package
([`Codevoid.Utilities.Mixpanel`](https://nuget.org/packages/Codevoid.Utilities.Mixpanel/))
to the NuGET.org feed — just add it to your UWP Project in Visual Studio, and
it'll be available for use in your code.

If you 

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

Adding a super property
-----------------------
Assuming you have an instance of `MixpanelClient`, you can set super properties
with three data types -- `String`, `Double`, and `Boolean`:
```
mixpanelClient.SetSuperProperty("PropertyName", "PropertyValue");
mixpanelClient.SetSuperPropertyAsDouble("DoublePropertyName", 3.14);
mixpanelClient.SetSuperPropertyAsBoolean("BooleanPropertyName", false);
```
_Note:_ if you're using a language that supports overloading (C++, C#), then you
don't need the extra names -- just `.SetSuperProperty`.

Once set, the super properties will attached to every event when logged,
automatically. This is useful for tracking against a single user, for example.

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

void Track(String name, IPropertySet properties)
------------------------------------------------
Adds the event with the supplied name & properties (in addition to any super
properties) to the upload queue. It is _not_ sent immediately, and there are no
promises/events to know when it's made it to the service.

```
mixpanelClient.Track("LoggedIn", null);
```

### Parameters
`name` — Name of the event that is being logged (required)
`properties` — Any additional properties you would like to log with this event (optional)

void SetSuperProperty(String name, [String/Double/Boolean] value)
-----------------------------------------------------------------
Sets a super property on the class, that is persisted across instances. You can
store `string`'s, `doubles`, and `booleans`. When consuming this from JavaScript,
you need to use the named versions of this method: `SetSuperPropertyAsDouble`
for doubles, and `SetSuperPropertyAsBoolean` for, obviously, `Booleans`.

```
mixpanelClient.SetSuperProperty("ASuperProperty", "SuperValue");
```

### Parameters
`name` — Name of the property that is to be set (required)
`value` — Value to be set for the supplied name (required)

string/double/bool GetSuperPropertyAsString/Double/Boolean(string name)
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
Clears all currently set super properties.

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
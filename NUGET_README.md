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

For more detailed usage, see the README.md in the package, or [here](https://github.com/grork/MixpanelClient/blob/master/README.md)

Release Notes
=============

v1.2.4
------
Fixed issue due to bugs with Co-routines in VS 2017 15.7 (would crash randomly)

v1.2.3
------
Fixed issue with handling corrupt JSON queue files (E.g. owning process was killed mid-write)
Publish PDBs

v1.2.2
------
Fixing issue with projection of overloaded methods into javascript. This is a _breaking change_, where all `SetXXProperty` Methods as now typed in their name.

v1.2.1
------
Fixing issue with automatic session events not being uploaded until the host app was completely restarted.


v1.2
----
Support for automatic sessions & session properties

v1.1.2
------
Fixed Crashing issue when attaching duration events

v1.1.1
------
Fixed issue not correctly accounting for time in the background

v1.1
----
Added support for automatic duration events (`StartTimedEvent`)

v1.0
----
Initial Release

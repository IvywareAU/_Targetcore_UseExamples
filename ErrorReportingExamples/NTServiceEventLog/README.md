# `NTServiceEventLog` — a hub hosted as a Windows service, reporting to the event log

A `P2PeerHub` hosted inside a Windows service by `P2PeerService`, with its
diagnostics going where a service's diagnostics have to go: the **Windows
application event log**.

There is a reason this example exists beyond "here is how to write a service".
A service is the one deployment shape in which a library has **nowhere to put a
diagnostic** — no console, no standard error, no desktop anybody is looking at —
and getting that wrong in a message router does not produce a missing log line.
It produces a hub that cannot stop.

---

## Why a dialog is a hang

`P2Pevent::Display()` emits an event as text, or as a modal `MessageBox` when
there is nowhere to write text. `MB_TASKMODAL` blocks **the thread that raised
the event** until somebody dismisses the dialog.

Events are raised on interior worker threads. `P2PeerCon::OnClose` runs on the
hub's **own pump**, and in service mode `P2PeerService::Run()` calls `RunHub()`
directly, so the service's main thread *is* a pump. `CloseHub()` then waits for
its pumps to leave, without a bound — deliberately, because bounding that wait
would let teardown run past a live pump and trade a hang for a use-after-free.

Chain it together:

```
  a diagnostic on the pump  ->  MessageBox on the pump thread
                            ->  nobody in session 0 can click OK
                            ->  the pump never returns from dispatch
                            ->  CloseHub() waits for it forever
                            ->  SCM STOP times out, service stuck STOP_PENDING
```

Every link was documented separately for a long time before anybody joined
them up.

**This is now closed in the library, and needs no configuration.**
`P2PeventUseTextOutput()` asks whether anybody *could see* a dialog before it
asks whether there is anywhere to *write* one — `ProcessIdToSessionId` for
session 0, and `WSF_VISIBLE` on the process window station. A service fails
both, so it never reaches the dialog branch. A windowed application is in
session ≥ 1 on a visible station, so it is unaffected and keeps its dialogs.

The `RunHub()` override in this example raises a deliberate `P2Pevent` **on the
pump thread** for exactly this reason. It is not padding — it is the shape that
used to deadlock, and the example should stop cleanly with it in place.

## What the two halves are

Refusing the dialog is only half a fix. The other half is a destination:
without one, the text goes to a standard error that is not there, and a defect
that used to hang loudly would instead vanish silently.

| | Who does it | Source name in the log |
| --- | --- | --- |
| **Library** diagnostics — every `P2Pevent` the kernel raises | `P2PeerService::Run()` installs a `P2Pevent` text sink in service mode. You get this for free. | the **service** name, `P2PmsgEventLogExample` |
| **Application** events — the ones only your code knows are events | `AppEventLog` in this example. No library can raise these for you. | `P2PmsgErrorReportingExample` |

Two sources on purpose. They are two different publishers, and an operator
should be able to tell "the router had a problem" from "the application had a
problem" without reading the text.

## The message table, and why events can look broken

`ReportEventW` puts text in the log whether or not anybody registered the
source. What registration buys is **formatting**. Without an `EventMessageFile`
the Event Viewer has no message table to render your insertion strings with, and
shows:

> The description for Event ID (3) in Source (P2PmsgErrorReportingExample)
> cannot be found. […] The following information is part of the event:

The text is still all there — it is in the event's insertion strings, and
`Get-WinEvent`'s `.Properties` will show it — but it reads as a broken product.
This is not unusual, incidentally: **41 of the 204** sources registered on a
stock Windows 11 machine ship no message file at all.

This example does not carry its own catalogue. It points `EventMessageFile` at
**`TargetCore.dll`**, which carries one built from `TargetCore/TargetCoreEvt.mc`
whose entries take two insertion strings and render them verbatim — `%1` the
origin, `%2` the message. That is all an application needs to get its own text
into the log, and it means this example adds no `mc.exe` step to any build.

If you want your own event IDs to alert on, write your own `.mc`, compile it with
`mc.exe`, link the result into your module, and name **that** module in
`AppEventLog::MessageFilePath()`. Nothing else in this example changes.

Event IDs here are the `P2Pevent_e` class values, so the mapping is a `switch`
rather than a table that can drift from the enum:

| `P2Pevent_e` | Event ID | Level |
| --- | --- | --- |
| `P2Pevent_ERROR` | 1 | Error |
| `P2Pevent_WARNING` | 2 | Warning |
| `P2Pevent_INFO` | 3 | Information |
| `P2Pevent_DEBUG` | 4 | Information |
| `P2Pevent_TRACE` | 5 | Information |
| `P2Pevent_LOG` | 6 | Information |
| `P2Pevent_REPORT` | 7 | Information |
| anything else, incl. `UNDEF` and `USER`*n* | 9 | Warning |

---

## Building

Needs `TargetCore.dll` and `Msgcore.dll` in `..\..\..\bin\<Configuration>64`,
as every example tree here does — the post-build step stages them beside the
executable and **fails the build** if the kernel is missing, because
`TargetCore` is delay-loaded and the failure would otherwise be `0xC06D007E` at
startup. For this example the DLL matters twice over: it also carries the
message table that `EventMessageFile` points at.

```
msbuild "ErrorReportingExamples(2026).sln" -p:Configuration=Debug -p:Platform=x64
```

> If the events you get are unformatted **and** you registered the source, check
> that the staged `TargetCore.dll` is a build that actually contains the message
> table — it was added on 2026-08-17. A DLL older than that has no table, and
> `EventMessageFile` will point at a module that cannot format anything.

## Running

```
  -I[nstall]   register the service and BOTH event sources   (ELEVATED)
  -R[emove]    deregister both                              (ELEVATED)
  -C[onsole]   run in this console, no SCM, text on stderr
  -S[ervice]   run as a service — what the SCM passes
  -Debug       add DEBUG events to the reporting mask
  -?           usage
```

### In a console, no elevation

```
NTServiceEventLog.exe -Console
```

The hub comes up, the pump reports itself, the deliberate pump-thread diagnostic
appears as **text on stderr** (a console is somewhere to write, so no dialog and
no log sink), and `Q` quits. The application's own events still go to the event
log — unformatted, since nothing is registered.

### As a real service

From an **elevated** prompt:

```
NTServiceEventLog.exe -Install
sc start P2PmsgEventLogExample
sc stop  P2PmsgEventLogExample
NTServiceEventLog.exe -Remove
```

`sc stop` returning promptly, and the service reaching `STOPPED` rather than
sitting in `STOP_PENDING`, **is** the property described at the top of this file.

> **This has now been run.** Elevated, on 2026-08-22, against a kernel rebuilt
> from Msgcore `3f62ca2`: the service reached **`STOPPED` in 40 ms**, with six
> records in the Application log across both sources. Figures and the full
> transcript are in `ProductionPlan.md`, Stage 0 step 1.
>
> **Read the log ordering, not just the stop.** A clean stop on its own proves
> nothing — an unarmed hub also stops promptly, because
> `P2PeerService::Run()` skips `RunHub()` when `CreateHub()` fails, so nothing
> ever runs. What proves the dialog was refused is the sequence inside one
> second: *"pump entering dispatch on thread N"*, then the deliberate
> `P2Pevent`, then *"pump left dispatch"*. A `MessageBox` would have parked
> that thread inside `Cancel()`, and the third line could never have been
> written.
>
> **`RequireAuth(false)` is why it arms at all.** The 2026-08-18 auth default
> reached the ten `DirectExamples` and missed this tree; without the migration
> now in `wmain`, this example refuses to arm and the run above reports a pass
> that means nothing. If you copy this example, copy that line and the comment
> above it — or provision the hub properly.

### Reading the result

```powershell
Get-WinEvent -LogName Application -MaxEvents 40 |
  Where-Object { $_.ProviderName -like 'P2Pmsg*' } |
  Format-List TimeCreated, ProviderName, Id, LevelDisplayName, Message
```

To see the raw insertion strings even when the source is unregistered:

```powershell
Get-WinEvent -LogName Application -MaxEvents 40 |
  Where-Object { $_.ProviderName -like 'P2Pmsg*' } |
  Select-Object -First 3 |
  ForEach-Object { $_.TimeCreated; $_.Properties | ForEach-Object { '  ' + $_.Value } }
```

Measured on 2026-08-17 from an **unelevated** console run, which is why the
messages are unformatted and the levels blank:

```
found 3 event(s)
22:01:55  provider=P2PmsgErrorReportingExample  Id=3  level=
      %1 = ReportingHub::RunHub
      %2 = pump left dispatch after 0 broadcast(s)
22:01:55  provider=P2PmsgErrorReportingExample  Id=3  level=
      %1 = ReportingHub::RunHub
      %2 = pump entering dispatch on thread 20260

registered? EventMessageFile for the app source:
      NOT REGISTERED (needs an elevated -Install)
```

That is the whole argument for registering the source, in one measurement: the
data is intact, the presentation is not.

---

## Reproducing the deadlock, if you want to see it

The fix is a policy, so it can be removed. In `Msgcore/Msgexception.cpp`, delete
the viewability clause from `P2Pevent::TextOutputPolicy()`:

```cpp
    if ( !bViewable )
      return true;                     // nobody could dismiss it: NEVER a dialog
```

rebuild `Msgcore`, stage it, `-Install` and `sc start`, then `sc stop`. The stop
will not complete: the service sits in `STOP_PENDING` with a `MessageBox` open on
a window station nobody is attached to. `sc queryex` will give you the PID, and a
thread dump shows the pump parked in `USER32!MessageBoxExW`.

`MscsUnitTests/p2p_servicedialog` asserts the same policy without needing a
service — a test process cannot move itself into session 0, so it drives
`TextOutputPolicy()` over all sixteen input combinations directly. Removing the
clause above fails exactly one of its rows, the one named
*"service under the SCM"*.

## Files

| File | |
| --- | --- |
| `NTServiceEventLog.h` | `AppEventLog`, `ReportingHub`, `ReportingService` |
| `NTServiceEventLog.cpp` | all three, plus the SCM entry points and `wmain` |

## Notes that cost somebody time

* **`-?` sets console mode.** `HelpMainArgs()` concludes that a help request came
  from a person at a terminal, so it sets `m_bConsole`. A `main()` that falls
  through from help into the run loop starts a hub and sits on the console menu.
  `wmain` here returns straight after printing.
* **`StartupP2Pmsg()` is yours to call.** `P2PeerService::Run()` does *not* do it
  — its call is commented out — and a hub cannot be created without it.
* **`Run()` calls `CleanupP2Pmsg()`** on the way out, so do not call it again.
* **The service owns its hub.** `~P2PeerService` deletes whatever
  `PostP2PeerHub()` was given.
* **An event log sink must never raise a `P2Pevent`.** It is reached *from*
  `Display()`, so an event raised inside it re-enters `Display()` and recurses
  until the stack is gone. Every failure path in `AppEventLog::Write()` is silent
  on purpose. If you write your own sink, carry this rule over.
* **No `DECLARE_P2PeerMsg_MAP` in `ReportingHub`.** The handlers it overrides are
  virtual in `P2PeerHub` and the base map dispatches through the vtable. A
  subclass needs its own map only for messages the base does not handle.
* **`PostDestroyHub()` looks like the natural teardown hook and is not called** —
  its only call site is commented out (`P2PeerHub.cpp:362`). `RunHub()` and the
  message handlers are the real pump-thread paths.

# `ErrorReportingExamples` — where a **Targetcore** diagnostic goes

The sibling trees in this repository are about getting messages from one hub to
another. This one is about what happens when something goes **wrong** while they
do, and specifically about where the resulting diagnostic ends up.

That sounds like a footnote and is not, for one reason: `P2Pevent::Display()`
falls back to a modal `MessageBox` when there is nowhere to write text, a modal
dialog blocks **the thread that raised the event**, and events are raised on
interior worker threads — a hub's own pump among them. A hub's teardown waits for
its pumps. So in any host where nobody can click OK, a diagnostic is not a
missing log line; it is a hub that cannot stop.

| | Example | Host shape | Destination |
| - | ------- | ---------- | ----------- |
| 1 | [`NTServiceEventLog`](NTServiceEventLog) | Windows service, under the SCM | the Windows application event log, two sources |
| 2 | [`DialogOrLogFile`](DialogOrLogFile) | a console harness, and three blind children it launches | a log file — or the modal dialog, on a switch in a text file |

The two are complements. `NTServiceEventLog` is the host that **cannot** show a
dialog and needs somewhere to report; `DialogOrLogFile` is the host that **can**,
and the setting that decides whether it does.

## Read this first

The library-side rule is short, and it is the thing to take away even if you
never host a service:

**A dialog is raised only where somebody could actually dismiss one.**
`P2PeventUseTextOutput()` asks whether a dialog would be *viewable* — session 0,
via `ProcessIdToSessionId`, and `WSF_VISIBLE` on the process window station —
*before* it asks whether there is anywhere to write instead. A service fails both
tests and never reaches the dialog. A windowed application passes both and keeps
its dialogs exactly as before. Nothing needs configuring either way.

The two extension points an application actually uses:

| | |
| --- | --- |
| `P2Pevent::SetTextSink ( fn )` | where text goes once the library has decided on text. Default is stderr; a host with no stderr installs one of these. **Not** the `Register4P2Pevents` notification sink — that is a single global slot, and a service claiming it would silence whatever its host had registered. |
| `P2Pevent::ForceTextOutput ( true )` | take the dialog off the table for the process, from code. Same effect as `P2PMSG_NO_UI=1` in the environment, but reversible and visible at the call site. |

And one that needs no code at all — a plain text file, `P2Pmsg.cfg`, beside the
host executable:

```ini
ErrToMessageBox: 1        # 1/0, on/off, yes/no, true/false
LogFile: errorLog.txt     # relative to THIS file's folder
```

[`DialogOrLogFile/P2Pmsg.cfg.sample`](DialogOrLogFile/P2Pmsg.cfg.sample) is that
file in full and commented: every accepted spelling, the search order, the
encoding rules, and both of the precedence rules below. Copy it beside the
executable you want to configure and rename it to exactly `P2Pmsg.cfg`. It
carries the `.sample` suffix, and nothing stages it, because one output directory
serves both examples in this tree — a live config file there would reconfigure
both of them, `NTServiceEventLog.exe` included.

Both settings are optional and the defaults are **the dialog on, no log file** —
which is byte for byte the behaviour that stood before the file was understood.
Two rules make it safe to leave lying around, and
[`DialogOrLogFile`](DialogOrLogFile) demonstrates each:

* **`ErrToMessageBox: 1` cannot put a dialog back where one would hang.** It
  moves in one direction only. `0` is a third way of saying what `P2PMSG_NO_UI`
  and `ForceTextOutput(true)` say; `1` merely declines to say it, and never
  overrides them or the viewability test.
* **`LogFile` loses to an installed `SetTextSink`.** It has to: `P2PeerService`
  installs the event log sink, and a stray config file must not silently divert a
  service's diagnostics into a file under `%SystemRoot%\System32` — which is
  where a service's working directory points. For the same reason a *relative*
  `LogFile` resolves against the config file's folder, never the working
  directory.

`P2PeerService` already uses both for a hosted hub, so a service gets the
library's own diagnostics in the event log without writing any of this. What no
library can do for you is raise the events that are **yours** — that is the part
`NTServiceEventLog` demonstrates.

## Related

* [`ArchitectureFAQ.md`](../ArchitectureFAQ.md) — hubs vs pumps, thread affinity,
  and what happens to an exception thrown inside a handler.
* [`DirectExamples`](../DirectExamples) — the transport harnesses. Every one of
  them includes `Msgexception.h`; none of them is about it.
* `Targetcore/SECURITY.md` — the posture table, including the row for this
  property, and why `CloseHub()`'s wait is deliberately unbounded.
* `Targetcore/TargetcoreEvt.mc` — the message catalogue these examples render
  through, and the regeneration command for it.

## Licence

Apache-2.0, as the rest of the repository. See [`LICENSE`](../LICENSE).

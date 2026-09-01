# `DialogOrLogFile` — the `ErrToMessageBox` switch, and what it costs to get it wrong

**TargetCore** reads a plain text file, `P2Pmsg.cfg`, from beside the host
executable:

```ini
ErrToMessageBox: 1        # 1/0, on/off, yes/no, true/false
LogFile: errorLog.txt     # relative to THIS file's folder
```

With the dialog **on** — the default — a diagnostic raised in a host that has
nowhere to write text becomes a modal `MessageBox`. With it **off**, the same
diagnostic is appended to the log file instead.

That would be a one-paragraph example if the difference were cosmetic. It is
not: `MB_TASKMODAL` blocks **the thread that raised the event** until somebody
dismisses it, and events are raised on interior worker threads — a hub's own
pump among them. So the two settings do not differ by where the text appears.
They differ by whether the raising thread ever runs again.

A fully commented [`P2Pmsg.cfg.sample`](P2Pmsg.cfg.sample) sits beside this file:
every accepted spelling, the search order, the encoding rules, and both
precedence rules in one place. It is deliberately **not** named `P2Pmsg.cfg` and
is **not** copied to the output directory — that directory holds more than one
example, and a live config file there would silently reconfigure all of them,
`NTServiceEventLog.exe` included.

## Running it

```powershell
.\DialogOrLogFile.exe              # every leg
.\DialogOrLogFile.exe -nodialog    # skip the leg that raises a real dialog
.\DialogOrLogFile.exe -keep        # leave the scratch files to look at
.\DialogOrLogFile.exe -?           # the settings, spelled out
```

Exit codes follow the tree convention: `0` pass, `1` setup failure, `3` a check
failed. **A dialog does appear on screen for a moment** during the default run —
see below for why, and for why nothing is left behind.

## The five legs

| | Leg | What it establishes |
| - | --- | ------------------- |
| 1 | the search | `P2Pmsg.cfg` placed beside the executable is found by `LoadConfigFile(0)`, and a **relative** `LogFile` resolves against the config file's own folder — never the working directory |
| 2 | `off`, no `LogFile` | turning the dialog off implies `errorLog.txt` beside the config file, and a raised event lands in it with its class and origin |
| 3 | `on`, `LogFile` named | a named log is honoured *whatever* chose text. This host is a console app, so it has a writable stderr and text wins regardless — naming a destination is explicit, and stderr is only a default |
| 4 | a value the parser cannot read | `ErrToMessageBox: maybe` is **ignored and reported** through `ConfigDiagnostic()`. Neither default is guessed at |
| 5 | three blind children | the shape where the setting actually decides — below |

Leg 1 is the only one that writes `P2Pmsg.cfg` where the library searches,
because that file governs every MSCS process started from that folder. It is
deleted again immediately. Every other leg names its file outright.

## Leg 5, and why it re-executes itself

A console harness cannot demonstrate this on its own: it *has* a standard error,
so text is chosen no matter what the file says. The dialog is only reachable in a
host with no console **and** no standard error — which is a service's shape, and
not this process's.

So each sub-leg re-executes this executable with `CREATE_NO_WINDOW`, and the
child then frees its console and NULLs its own standard error. It keeps its
session and window station, so a dialog raised there really does reach the
desktop.

| | Child | Result |
| - | ----- | ------ |
| (a) | `ErrToMessageBox: 0` | **no dialog.** Exits, and its log file holds the diagnostic |
| (b) | `ErrToMessageBox: 1` | **a dialog.** Raised, observed, and nothing logged |
| (c) | `ErrToMessageBox: 1` **plus `P2PMSG_NO_UI=1`** | **no dialog.** The file loses to the environment |

Each child writes what it resolved — session, window-station visibility,
standard error, console, and the resulting decision — to a small `.state` file
*before* raising anything, because a child that blocks writes nothing
afterwards. The parent prints it either way.

### (c) is the one to read twice

`ErrToMessageBox: 1` **cannot put a dialog back where one would hang.** Setting
it to `0` is a third way of saying what `P2PMSG_NO_UI=1` and
`P2Pevent::ForceTextOutput(true)` already say; setting it to `1` merely declines
to say it. The setting moves in one direction only, and leg (c) is the proof: the
file asks for a dialog, the environment has refused one, and the child comes
back.

A deployment artefact dropped beside an executable must not be able to re-arm a
deadlock that a host had already disarmed. That is not a preference — it is the
whole defect this area exists to prevent.

## Two things this harness got wrong first, and what they cost

Both are recorded because the mistakes are more instructive than the fix, and
because each produced a *green* or *stable-looking* result while being wrong.

**`DETACHED_PROCESS` was the wrong way to blind the child.** A detached child's
`MessageBox` appears and then **closes itself** after a couple of seconds,
whereupon the process exits `0`. The harness saw a child that returned, concluded
nothing had blocked, and reported the opposite of the truth — while a dialog was
sitting on the screen, owned by the very pid it had just called healthy. The
window was visible in `Get-Process` the whole time. `CREATE_NO_WINDOW` plus the
child's own two removals is a truer model anyway: a service has a session, a
station and a desktop; what it lacks is a console and a standard error.

**The verdict must not be "the child was still running".** Even with the right
flags, that gate failed about one run in four: on a live desktop *something else*
closes the message box, and the child then exits well inside the timeout.
Nothing about the library differed between a pass and a fail. So the harness
watches for the **dialog window** instead — class `#32770`, owned by the child's
pid. A modal dialog having been *raised* is the property the setting controls, it
is observable the instant it happens, and no third party can un-observe it.
Whether the child then stays blocked is printed and **not asserted** — the same
call the sibling harness made about `pump left dispatch`, and for the same
reason: an intermittent harness gets ignored, and then so does everything it
says.

The block itself is not thereby unproven. It is proven for the shape that
matters by [`NTServiceEventLog`](../NTServiceEventLog) under the SCM, where there
is no desktop and so nothing to close the dialog at all.

## What it links, and what it does not

This harness needs no kernel. Its whole subject is `P2Pevent`, which lives in
**Msgcore**, and it raises events directly — so it links `Msgcore.lib` alone and
stages `Msgcore.dll` alone. No `TargetCore`, no delay-load, no
`DelayLoadReport.cpp`. That is a statement rather than an omission: naming a
library it never calls would claim a dependency that does not exist.

## One more thing worth knowing

**Turning the dialog off does not turn reporting on.** The notification mask and
the output policy are separate configurations and neither implies the other. Out
of the box the mask is `ERROR` alone, so a `WARNING` raised without
`Configure(ADDMASK, ...)` reaches neither the dialog nor the log. Leg 3 adds it
explicitly, and failed three checks on exactly this before it did.

## Related

* [`../README.md`](../README.md) — the tree, and the library-side rule in short
* [`NTServiceEventLog`](../NTServiceEventLog) — the same subject under the SCM,
  reporting to the Windows event log
* `MscsUnitTests/p2p_servicedialog` — the verdict on the rule, on both
  platforms: the policy over all sixteen input combinations, plus the
  configuration file including the one-way property leg (c) demonstrates here
* `Msgcore/Msgexception.h` — `LoadConfigFile`, `LogFilePath`, `SetLogFile`,
  `ConfigDiagnostic`, and the precedence rules in full

## Licence

Apache-2.0, as the rest of the repository. See [`LICENSE`](../../LICENSE).

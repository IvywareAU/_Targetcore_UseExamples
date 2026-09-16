# `DirectExamples` — worked examples for the **Targetcore** library

Twelve console harnesses for `MSCS/Targetcore` — the message **transport** library:
`P2PeerHub`s, the pump threads that drive them, the `P2PeerCon` transports that
join them, the login handshake, and `P2PeerMsg` routing between them.

This tree is about **moving** messages. The sibling
[`_Msgcore_UseExamples\DirectExamples`](../../_Msgcore_UseExamples/DirectExamples) is about what is **in** one —
`P3PmsgData`, `P3PmsgField`, `P2PmsgMgr` and the relocation-safe heap underneath
them. Every harness here links `Msgcore.lib` and includes `Msgexception.h`, but
none of them opens a payload: to this tree a message body is an opaque byte
range.

| | `DirectExamples` | `_Msgcore_UseExamples\DirectExamples` |
| --- | --- | --- |
| Subject | moving messages between hubs | what is *in* a message |
| Written against | `P2PeerHub`, `P2PeerCon`, `P2PeerMsg` | `P3Pmsg*`, `P2PmsgMgr`, `Msgcore_c.h` |
| Links | `Targetcore.lib` + `Msgcore.lib` | `Msgcore.lib` (+ `Targetcore.lib` for two) |
| Networking | every harness | the last two only |

Two long-form documents sit beside this one and are not repeated here:

* [`ArchitectureFAQ.md`](../ArchitectureFAQ.md) — hubs vs pumps, thread affinity, the
  login handshake, what happens to an exception thrown inside a handler, and how
  this tree relates to the `FacadeExamples`, `ComExamples`, `dotNetExamples` and
  `PanamaJavaExamples` re-implementations.
* [`About.md`](About.md) — what `AlexTest` and `AlexInterop` are, why there are two
  of them, and why only one of the two can be run headless.

---

## The twelve

`AlexTest` is the ancestor; the lineage of the whole tree runs
`AlexTest → PipeMeshTest → {WsaMeshTest, DmxMeshTest → Com232MeshTest}`. Read it
first, then whichever transport you care about.

| # | Harness | Transport | Subject |
| - | ------- | --------- | ------- |
| 1 | [`AlexTest`](AlexTest) | loopback TCP, **two processes** | the original probe: one exe as both server and client, one `P2Pmsg_BCast` across a real process boundary |
| 2 | [`AlexInterop`](AlexInterop) | loopback TCP, **two processes** | the same probe rewritten to be portable and headless — the Linux port's Phase-3 exit criterion |
| 3 | [`PipeMeshTest`](PipeMeshTest) | `P2PeerConPipe` | `AlexTest` collapsed into one process: two hubs, two pump threads, a named pipe between them |
| 4 | [`WsaMeshTest`](WsaMeshTest) | `P2PeerConWsa` | the same shape over loopback TCP — the harness that asks whether the historical in-process TCP failure still fires |
| 5 | [`DmxMeshTest`](DmxMeshTest) | `P2PeerConDmx` | DMX (Direct Memory eXchange), the in-address-space transport: a real connection with no OS handle, endpoints matched by service name |
| 6 | [`Com232MeshTest`](Com232MeshTest) | `P2PeerCon232` | RS-232 over a `com0com` null-modem pair, and the `WaitCommEvent` arming order that makes or breaks the handshake |
| 7 | [`LocalInMemoryTest`](LocalInMemoryTest) | none | `PostP2Pmsg()` — delivery with no connection, no handshake and no `P2PeerCon` at all |
| 8 | [`PipeMsgMapTest`](PipeMsgMapTest) | `P2PeerConPipe` | `BEGIN_P2PeerMsg_MAP` request/response: a named message routed across the wire and answered from inside the handler |
| 9 | [`PipeMsgFactoryTest`](PipeMsgFactoryTest) | `P2PeerConPipe` | the same round trip built through `P2PeerMsg::RedirectFactory` instead of a hand-constructed `P2PeerMsg32` |
| 10 | [`TwoConTest`](TwoConTest) | `P2PeerConWsa` ×2 | can **one** hub supervise two connections and log in to itself? |
| 11 | [`ExplorerTest`](ExplorerTest) | `P2PeerConWsa` | standing up a `P2PeerExplorer` expump, and the four rules a client has to obey to talk to one |
| 12 | [`RouteLoopbackTest`](RouteLoopbackTest) | none | the tree's outlier: `PeerNetwork::route()` in the **`treehub_runtime`** engine — no Targetcore, no MFC |

---

## The exit-code contract

Every harness except `AlexTest` reports its verdict as the process exit code, so
a headless run is unambiguous:

| Code | Meaning |
| --- | --- |
| `0` | SUCCESS |
| `1` | SETUP — a prerequisite was missing (startup or factory failure) |
| `2` | an MFC/CRT assertion fired — caught by `_CrtSetReportHook`, which is what stops a modal dialog hanging an unattended run |
| `3` | TIMEOUT — the awaited event did not fire inside the deadline |

`AlexTest` is the exception, and deliberately so: its server blocks on `getchar()`
and `main()` returns `0` unconditionally, so its exit code carries no verdict at
all. It is interactive by design and **cannot be adjudicated by a script**. That
gap is exactly what `AlexInterop` was written to close — see
[`About.md`](About.md).

`AlexTest` and `AlexInterop` are also the only two that do not install the
`_CrtSetReportHook` assert hook, so neither can produce exit code `2`.

---

## Building

One solution at the root builds all twelve into a single shared `out\` tree.
There is no per-project `.sln`; build one harness with MSBuild's `/t:`.

```
DirectExamples\
  DirectExamples(2026).sln        all twelve
  <Harness>\<Harness>(2026).vcxproj
  out\x64\{Debug,Release}\                 exes + staged runtime DLLs
  out\x64\{Debug,Release}\obj\<Harness>\   intermediates
```

```powershell
$msbuild = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
& $msbuild ".\DirectExamples(2026).sln" /p:Configuration=Debug /p:Platform=x64
& $msbuild ".\DirectExamples(2026).sln" /t:WsaMeshTest /p:Configuration=Debug /p:Platform=x64
.\out\x64\Debug\WsaMeshTest.exe
```

x64 only, `v145`, `stdcpp17`, `UseOfMfc=Dynamic`, Unicode. Configurations are
`Debug` and `Release`; there is no static-link shape here.

[`run_all.ps1`](run_all.ps1) builds the tree and runs every harness that *can* be
run unattended, then prints a pass/fail/setup table:

```powershell
.\run_all.ps1                     # build + run Debug
.\run_all.ps1 -Config Release
.\run_all.ps1 -NoBuild
```

---

## The sibling dependencies

**A clone of this repository alone does not build.** These harnesses are examples
*of* two libraries that are not vendored here, and the project files reach outside
the repository in four distinct ways. All four are relative paths that assume this
tree is checked out inside the parent MSCS solution — they are not submodules, and
there is no fallback.

| # | What is reached | Where from | Needed by |
| - | --------------- | ---------- | --------- |
| 1 | `..\..\..\Msgcore` and `..\..\..\Targetcore` | headers, at compile time | all 12 except `RouteLoopbackTest` |
| 2 | `..\..\..\lib\$(Platform)\$(Configuration)\{Msgcore,Targetcore}.lib` | import libraries, at link time | all 12 except `RouteLoopbackTest` |
| 3 | `..\..\..\bin\$(Configuration)64\{Msgcore,Targetcore}.dll` | staged by a post-build `xcopy`, at run time | all 12 except `RouteLoopbackTest` |
| 4 | `..\..\..\vsutils\DelayLoadReport.cpp` | compiled in, to report a `/DELAYLOAD` fault legibly | 9 of the 12 |

The post-build step fails the build if `Targetcore.dll` is missing rather than
letting it pass: `xcopy` exits `0` on a wildcard miss, and without that check the
exe would build green and then die at startup with `0xC06D007E`.

`RouteLoopbackTest` has a **fifth, entirely separate** dependency and is the reason
it is called the outlier: it links `treehub_runtime.lib` from `$(KgnRoot)`, an
environment variable pointing at a different project altogether. `KgnRoot` is not
defined anywhere in the MSCS tree. On a machine without it, that one project fails
to build and the other eleven are unaffected — build them with `/t:` rather than
trying to build the solution.

### What that means for CI

Stated plainly, because a green tick that verified nothing is worse than no tick:

* [`ci.yml`](../.github/workflows/ci.yml) runs on every push and **compiles nothing**.
  It checks what this repository can check about itself — that the solution and the
  project files agree, that every source they name exists, that the four sibling
  bindings above are still exactly four, and that the shipped Markdown does not link
  to files that are gone.
* [`solution-build.yml`](../.github/workflows/solution-build.yml) is the one that
  really builds and really runs the harnesses, and it is **`workflow_dispatch`-only**
  because it needs the siblings supplied to it. If they are not, it **fails** — it
  does not print "skipped" and exit `0`.

---

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](../LICENSE) for the
full text.

# `_TargetCore_UseExamples`

Worked examples for **TargetCore** — the MSCS message *transport* library:
`P2PeerHub`s, the pump threads that drive them, the `P2PeerCon` transports that
join them, the login handshake, and `P2PeerMsg` routing between them.

This repository holds **five trees**. They are not five different subjects —
they are the **same harnesses, five times over**, once for each way a caller can
reach the library. That is the whole point of the layout: put them side by side
and the difference you are looking at is the *binding*, never the material. The
questions asked, the verdicts printed and the exit codes returned are the same
in all five, deliberately, so a disagreement between two trees is a finding.

| Tree | Language | Reaches TargetCore through | Built by |
| --- | --- | --- | --- |
| [`DirectExamples`](DirectExamples) | C++ | `TargetCore.lib` and MFC — the C++ classes themselves | `DirectExamples(2026).sln` |
| [`FacadeExamples`](FacadeExamples) | C++ | `TargetFacade.dll`, a macro-free flat-vtable facade | `FacadeExamples(2026).sln` |
| [`ComExamples`](ComExamples) | C++ (+ PowerShell, VBScript) | `TargetCom`, an ATL dual-interface COM server over the facade | `ComExamples(2026).sln` |
| [`dotNetExamples`](dotNetExamples) | C# | the same COM server, by vtable and late-bound | `build.ps1` (Roslyn `csc`) |
| [`PanamaJavaExamples`](PanamaJavaExamples) | Java | the facade's raw vtables, through Panama FFI — no JNI, no jextract | `build.ps1` (`javac`) |

There is **no solution at this root, by design.** Each tree builds on its own —
different toolchain, different prerequisites, different failure modes — and a
solution spanning all seven would claim a build relationship that does not exist.
Start in the tree you care about; its own README is the documentation.

### And two trees that are not bindings

The five above vary the *binding* and hold the subject fixed. These two vary the
**subject** instead, so they are listed apart rather than as further rows —
reading either against `DirectExamples` tells you nothing about bindings.

| Tree | Language | Subject | Built by |
| --- | --- | --- | --- |
| [`ErrorReportingExamples`](ErrorReportingExamples) | C++ | where a diagnostic **goes** — and why, in a host with nowhere to put one, that decides whether a hub can stop at all | `ErrorReportingExamples(2026).sln` |
| [`SecurityExamples`](SecurityExamples) | C++ | the **posture** a hub runs in: one mixed-transport claim made twice, with the security defaults opted out and then provisioned | `SecurityExamples(2026).sln` |

---

## Read this first, whichever tree you are in

[`ArchitectureFAQ.md`](ArchitectureFAQ.md) lives at this root rather than inside
any one tree, because what it answers is true underneath all seven: hubs vs
pumps, thread affinity, the login handshake, what happens to an exception thrown
inside a handler, and why an in-process mesh behaves the way it does. Nothing in
it is repeated in the tree READMEs, and the sibling `_Msgcore_UseExamples`
repository links into it too.

---

## The harnesses

`DirectExamples` carries **twelve**; the other four re-implement the eleven that
can be re-implemented. The lineage runs
`AlexTest → PipeMeshTest → {WsaMeshTest, DmxMeshTest → Com232MeshTest}`.

| # | Harness | Transport | Subject |
| - | ------- | --------- | ------- |
| 1 | `AlexTest` | loopback TCP, **two processes** | the original probe: one exe as both server and client, one broadcast across a real process boundary |
| 2 | `AlexInterop` | loopback TCP, **two processes** | the same probe rewritten portable and headless |
| 3 | `PipeMeshTest` | `P2PeerConPipe` | `AlexTest` collapsed into one process: two hubs, two pump threads, a named pipe between them |
| 4 | `WsaMeshTest` | `P2PeerConWsa` | the same shape over loopback TCP |
| 5 | `DmxMeshTest` | `P2PeerConDmx` | DMX (Direct Memory eXchange), the in-address-space transport: a real connection with no OS handle |
| 6 | `Com232MeshTest` | `P2PeerCon232` | RS-232 over a `com0com` null-modem pair, and the arming order the handshake depends on |
| 7 | `LocalInMemoryTest` | none | delivery with no connection, no handshake and no `P2PeerCon` at all |
| 8 | `PipeMsgMapTest` | `P2PeerConPipe` | request/response: a named message routed across the wire and answered inside the handler |
| 9 | `PipeMsgFactoryTest` | `P2PeerConPipe` | the same round trip built through a redirect factory instead of by hand |
| 10 | `TwoConTest` | `P2PeerConWsa` ×2 | can **one** hub supervise two connections and log in to itself? |
| 11 | `ExplorerTest` | `P2PeerConWsa` | standing up an explorer expump, and the four rules a client has to obey to talk to one |
| 12 | `RouteLoopbackTest` | none | the outlier: routing in the `treehub_runtime` engine — no TargetCore, no MFC |

Every harness except `AlexTest` reports its verdict as the process exit code, so
a headless run is unambiguous:

| Code | Meaning |
| ---- | ------- |
| `0` | success — every check passed |
| `1` | setup failure (startup / factory / connect) |
| `2` | an assertion fired |
| `3` | a check failed, or nothing was delivered before the timeout |

`AlexTest` is the exception and cannot be run headless — it blocks on
`getchar()` and returns `0` unconditionally. Every runner builds it and none
runs it; `AlexInterop` is the automatable rewrite. See
[`DirectExamples/About.md`](DirectExamples/About.md).

---

## What each tree is actually for

### [`DirectExamples`](DirectExamples) — the library as its author wrote it

Twelve MFC-dynamic console harnesses written straight against the exported C++
classes: `P2PeerHub`, `P2PeerCon` and its five transports, `P2PeerMsg` and the
message map. They link `TargetCore.lib` and `Msgcore.lib`, and none of them
opens a payload — to this tree a message body is an opaque byte range.

This is the reference tree, and the only one that documents most harnesses in
**their own README**. Read it first.

### [`FacadeExamples`](FacadeExamples) — the same thing without the macros

Every harness rewritten on **TargetFacade**, the flat-vtable facade DLL. Same
questions, same verdicts, same exit codes, so the two trees read side by side —
and everything underneath changes: no MFC, no message-map macros, `HRESULT`s
instead of exceptions.

### [`ComExamples`](ComExamples) — the same thing from outside the process

Every harness again on **TargetCom**, the ATL dual-interface server over the
facade, plus late-bound **PowerShell** and **VBScript** clients under `script\`.
These executables link **nothing of MSCS** — only `ole32`, `oleaut32`, `uuid`
and a generated type-library header. An STA client must pump messages; that
constraint, and the rest of what COM imposes here, is in
[`COM_dependancy.md`](ComExamples/COM_dependancy.md).

### [`dotNetExamples`](dotNetExamples) — the same thing from a managed runtime

The same harnesses in **C#**, over the same COM server, early-bound by vtable
and late-bound through `IDispatch`. Built by Roslyn `csc` out of
[`build.ps1`](dotNetExamples/build.ps1) — there is no `.csproj` in this tree.

It is where the managed/native seam gets measured: what an agile CCW does to
apartment marshalling, what `ClassInterface(None)` does to `QI(IDispatch)`, and
what the CLR does to an `HRESULT` on the way back.

### [`PanamaJavaExamples`](PanamaJavaExamples) — the same thing with no native code at all

The same harnesses in **Java**, over the facade's raw vtables through the
**Panama FFI**: no JNI, no jextract, no generated bindings, no native code of
this tree's own. An MFC extension DLL initialises inside a bare JVM, and upcalls
land on the kernel's own pump threads — which is the interesting part, and the
part with the rules (shared arenas, and catching `Throwable` at every boundary).

### [`SecurityExamples`](SecurityExamples) — the same claim, opted out and then armed

Two C++ harnesses, `MixConTest` and `MixConTestAuth`, that ask one question —
can a single hub hold a `P2PeerConWsa` and a `P2PeerConPipe` at the same time? —
of two different postures. The first opts out with `RequireAuth(false)` and
`RequireSeal(false)`; the second provisions its three hubs and leaves both
defaults alone.

The pair is a **differential**, which is why the weaker half is kept rather than
superseded. `MixConTest` isolates the transport claim, so a failure on one side
and not the other says immediately whether you are debugging transports or a
key file. And `MixConTestAuth` alone could pass for the wrong reason — a hub
that never armed refuses every peer — so its pass condition is
`AuthArm() == ArmOk` and deliberately not the `ArmNotRequired` its sibling
reports. The failure the tree exists to catch is not "auth broke"; it is **"auth
was never on"**.

`ErrorReportingExamples` has no section here; it is documented entirely in
[its own README](ErrorReportingExamples/README.md), because what it covers —
where a diagnostic goes when nobody can dismiss a dialog — has no counterpart in
any other tree to compare it against.

---

## Building

Each tree builds independently and documents its own prerequisites. In outline:

```powershell
cd DirectExamples     ; .\run_all.ps1                  # build + run Debug
cd FacadeExamples     ; .\run_all.ps1 -Config Release
cd ComExamples        ; .\run_all.ps1 -IncludeScripts  # also the script clients
cd dotNetExamples     ; .\run_all.ps1
cd PanamaJavaExamples ; .\run_all.ps1
cd SecurityExamples   ; .\run_all.ps1 -Fresh    # also re-provision the keys
```

Each `run_all.ps1` builds its tree, runs the harnesses, prints a pass/fail table,
and **exits with the number of failures**.

Two harnesses need something a normal machine does not have, and every runner
treats them as SKIP rather than as failures: `Com232MeshTest` needs a `com0com`
COM5↔COM6 pair, and `RouteLoopbackTest` links `treehub_runtime.lib` from a KGN
project that is not part of the MSCS tree at all.

### The sibling dependencies

None of these trees builds standalone, and that is a property of the material
rather than an oversight. Every path below is **relative**, resolved from a
project file at `<repo>/<Tree>/<Harness>/`, and assumes this repository is
checked out inside the parent MSCS solution as `MSCS\_TargetCore_UseExamples`:

| | Reached | Wanted by |
| - | ------- | --------- |
| 1 | `..\..\..\Msgcore`, `..\..\..\TargetCore` | headers, at compile time — the three MSBuild trees that link the library itself: `DirectExamples`, `ErrorReportingExamples`, `SecurityExamples` |
| 2 | `..\..\..\lib\$(Platform)\$(Configuration)\*.lib` | import libraries, at link time — the same three |
| 3 | `..\..\..\bin\$(Configuration)64\*.dll` | staged by a post-build `xcopy`, at run time |
| 4 | `..\..\..\vsutils\DelayLoadReport.cpp` | compiled in, to report a `/DELAYLOAD` fault legibly |
| 5 | `..\..\..\TargetFacade` | the facade and its COM server — the other four trees |
| 6 | `$(KgnRoot)` | `RouteLoopbackTest` alone, and it is not an MSCS project |

**Three leading `..\` and not two.** Each tree used to be a repository of its
own, sitting directly under `MSCS\`; combining them put every tree one directory
deeper. `SecurityExamples` arrived the same way and for the same reason — its
two harnesses were loose directories under `MSCS\`, each with a solution of its
own, and both gained a level on the way in.
`.github/ci/check_repo_invariants.py` pins these paths per tree for exactly that
reason: a level lost in a move fails there, on a runner with no compiler, in
seconds — instead of surfacing as `LNK1181` on somebody's machine.

`vsutils\` is **not published anywhere**, and `Msgcore`, `TargetCore` and
`TargetFacade` are private repositories. See [`CONTRIBUTING.md`](CONTRIBUTING.md)
and the two workflows for what that means for CI.

---

## Continuous integration

Stated plainly, because a green tick that verified nothing is worse than no tick
at all:

* **`ci.yml`** runs on every push and **compiles nothing.** It runs
  `.github/ci/check_repo_invariants.py`, which checks bookkeeping only:
  solution/project parity for both configurations in all five MSBuild trees,
  that every source named exists, that the pinned outward paths and the paths
  built in the `.props` files are unchanged, and that the shipped Markdown does
  not link to files that are gone.
* **`solution-build.yml`** is the one that really builds and runs, and it is
  `workflow_dispatch`-only because it needs sibling checkouts that cannot be
  supplied automatically.

---

## The other family

The sibling repository [`_Msgcore_UseExamples`](../_Msgcore_UseExamples) is laid
out the same way and covers the other half: **what is in a message** — the typed
cells, the named fields, the manager and the relocation-safe heap underneath
them — rather than how one gets from hub to hub.

---

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.
Licensed under the Apache License, Version 2.0. See [`LICENSE`](LICENSE) for the
full text.

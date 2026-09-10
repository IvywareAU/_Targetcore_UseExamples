# About `AlexInterop` and `AlexTest`

What these two projects do, and how they differ from the other ten harnesses in this
repository.

---

## Summary

`AlexTest` is the **original** harness of this tree — the ancestor every other example is
written against. `AlexInterop` is that same probe rewritten as an **automatable, portable**
two-process test, and it serves as the Linux port's Phase-3 exit criterion.

They are the only **two-process** examples here. Every other harness collapses both hubs into
a single process.

---

## `AlexTest` — the original

A two-process MFC console application. The same executable is both roles:

```
AlexTest.exe                                   -- SERVER, listens on port 7777
AlexTest.exe send 127.0.0.1 "Hello from client!" -- CLIENT, connects and sends one message
```

Each process hosts one `P2PeerHub` on its own `SpawnHub()` pump thread. They connect over
real loopback TCP via `P2PeerConWsa`, run the TargetCore login handshake, and the client
posts a single `P2Pmsg_BCast` that the server prints.

`AlexTestHub` derives from `P2PeerHub` and overrides:

| Handler | Purpose |
|---|---|
| `On_P2PeerBCast` | server: print any arriving broadcast |
| `On_P2PeerUCast` | server: print any arriving unicast |
| `On_ConLoginAck` | client: handshake complete — post the message now |
| `On_ConStartup` / `On_ConConnect` / `On_ConAccept` / `On_ConListen` / `On_ConLogin` / `On_ConClose` / `On_ConShutdown` | lifecycle tracing; each prints the stage reached, then delegates to the base |

It is the only project in the tree with a full `README.md` (`AlexTest/README.md`), and the
sibling harnesses cite it by name in their own headers:

- `PipeMeshTest` — *"It mirrors AlexTest exactly, with two differences"*
- `WsaMeshTest` — mirrors `PipeMeshTest`, swapping the pipe back for loopback TCP
- `DmxMeshTest` — mirrors `PipeMeshTest`, transport `P2PeerConDmx`
- `Com232MeshTest` — mirrors `DmxMeshTest`, transport `P2PeerCon232`

So the lineage of the whole tree runs `AlexTest → PipeMeshTest → {WsaMeshTest, DmxMeshTest →
Com232MeshTest}`.

### Its one structural weakness

`AlexTest` **cannot be run headless.** The server blocks on `getchar()` waiting for a human to
press Enter, and `main()` returns `0` unconditionally at the end of the run. A client timeout
prints `TIMEOUT - no login-ack within 10s` and then still exits `0`. The exit code therefore
carries no verdict, and CI cannot tell a delivered message from a failed one.

That gap is precisely what `AlexInterop` was written to close.

---

## `AlexInterop` — the portable, automatable rewrite

Source file: `AlexInterop/alex_test.cpp`. Same hub class, same P2P addresses
(`AlexTest.Server` / `AlexTest.Client`), same single BCast — restructured so a script can
adjudicate it, and stripped of every Win32-ism in its I/O so the same source compiles under
g++ against the io_uring shim.

```
AlexInterop.exe server [port]              -- listen, wait for the client's BCast, exit
AlexInterop.exe send [ip] [port] [text]    -- connect, wait for login-ack, post one BCast, exit
```

**Verdict is the process exit code, on both sides:**

| Code | Meaning |
|---|---|
| `0` | SUCCESS — server: received the client's BCast (proves cross-process delivery). client: handshake completed + BCast posted. |
| `3` | TIMEOUT — the awaited event did not fire within the deadline (server 15 s, client 10 s). |
| `1` | SETUP — startup / factory failure. |

The **server's** exit `0` is the authoritative proof: it is the only signal that the message
actually crossed a process boundary.

### What was changed for portability

| `AlexTest` (Win32) | `AlexInterop` (portable) |
|---|---|
| `_setmode(_fileno(stdout), _O_U16TEXT)` | dropped |
| `wprintf(L"%s", wide)` | narrow `std::printf` via a `Log()` helper (glibc treats `%s` as narrow) |
| `GetLocalTime` / `SYSTEMTIME` timestamps | `pid`-stamped lines only |
| wide string literals throughout | `N(const wchar_t*)` wide→narrow converter at each print site |
| hardcoded port `7777` | port is `argv`-configurable, default `7811` |
| server blocks on `getchar()` | server blocks on `WaitForSingleObject` with a 15 s deadline |

Its header block also carries the Linux build line verbatim:

```
g++ -std=c++23 -fpermissive -D_UNICODE -DUNICODE -I. -I../Msgcore \
    -I../TargetCore -I../Msgcore/Platform -I../Msgcore/Platform/win-compat alex_test.cpp \
    -L../build/TargetCore -ltargetcore -L../build/Msgcore -lmsgcore -luring \
    -Wl,-rpath,../build/TargetCore -Wl,-rpath,../build/Msgcore -o alex_test
```

`ArchitectureFAQ.md:1101` records its role:

> `AlexInterop` is the Linux port's Phase-3 exit criterion and is written to avoid every
> Win32-ism so the same source builds against the io_uring shim with g++. […] **For the Linux
> port, use the original.**

Its own header states the distinction from `WsaMeshTest` sharply: each side drives *its own*
`TargetCore` pump on *its own* io_uring ring in a *separate address space* — unlike
`wsa_mesh.cpp`, which puts both hubs in one process on one ring.

---

## How the two differ from the other ten harnesses

| | `AlexTest` | `AlexInterop` | The other ten |
|---|---|---|---|
| Processes | **2** | **2** | 1 (two hubs, two pump threads, one address space) |
| Verdict mechanism | **none** — always `return 0` | exit code `0`/`1`/`3` | exit code `0`/`1`/**`2`**/`3` |
| `_CrtSetReportHook` assert hook | **no** | **no** | yes |
| Driven by | **a human pressing Enter** | timed `WaitForSingleObject` | timed `WaitForSingleObject` |
| `CWinApp theApp` | yes | **no** | yes (except `RouteLoopbackTest`) |
| Command-line arguments | yes | yes | **no**, except `ExplorerTest`'s optional port |
| `README.md` | yes | **no** | 7 of 10 |

Three points stand out.

**1. They are the only two-process tests.** `WsaMeshTest`, `PipeMeshTest`, `DmxMeshTest`,
`Com232MeshTest`, `LocalInMemoryTest`, `PipeMsgMapTest`, `PipeMsgFactoryTest` and
`TwoConTest` all run both hubs inside one process. `WsaMeshTest` is exactly `AlexTest`'s
transport collapsed into a single process — it exists to ask whether the historical
in-process loopback-TCP failure (`ASSERT(pCon==nullptr)` at `P2Pwin32.cpp:3844`) still fires
against the current DLLs. `AlexInterop` answers the complementary question that no
single-process harness can: does the message cross a real process boundary?

**2. Neither installs the assert hook.** The other nine TargetCore harnesses carry:

```cpp
_CrtSetReportHook(AssertReportHook);   // a debug ASSERT would pop a MODAL DIALOG
                                       // and hang a headless run
```

which is what backs the shared `2 = an MFC/CRT assertion fired` exit code. `AlexTest` and
`AlexInterop` have no such hook, so a debug ASSERT in either would pop a modal dialog and
hang rather than producing exit code `2`. For `AlexTest` that is consistent — it is
interactive by design. For `AlexInterop`, which is otherwise built for unattended runs, it is
the one remaining gap in its headless contract.

**3. `RouteLoopbackTest` is the tree's real outlier**, not these two. It is the only project
that does not touch TargetCore at all: it links `treehub_runtime.lib`, compiles as C++20, and
uses no MFC and no `CWinApp`. Every other project here links
`Msgcore.lib;TargetCore.lib;MsWsock.lib;ws2_32.lib;comsuppwd.lib;delayimp.lib` and compiles
as C++17.

---

## Build configuration

Both projects are registered in `DirectExamples(2026).sln` and set to build in both
solution configurations. Their settings are uniform with the rest of the tree:

- `ConfigurationType` Application, `UseOfMfc` **Dynamic**, `CharacterSet` Unicode
- `PlatformToolset` v145, `LanguageStandard` `stdcpp17`, x64 only
- `/DELAYLOAD:TargetCore.dll` plus `..\..\..\vsutils\DelayLoadReport.cpp` compiled in
- output to the shared `out\$(Platform)\$(Configuration)\` tree at the repository root
- post-build step stages `Msgcore.dll` / `TargetCore.dll` from `..\..\..\bin\$(Configuration)64\`
  and **fails the build** if `TargetCore.dll` is missing (`xcopy` exits `0` on a wildcard
  miss, so without that check the exe would die at startup with `0xC06D007E`)

Diffed against `WsaMeshTest`, the `AlexTest` project file differs only in its `ProjectGuid`
and two omitted lines (`<SDLCheck>false</SDLCheck>` and `<ProgramDatabaseFile>` in the Release
configuration). `AlexInterop` additionally lacks a `.vcxproj.filters` and `.vcxproj.user`.

### The `CWinApp` question — resolved

`AlexInterop/alex_test.cpp` declares **no `CWinApp theApp`**, even though the project is
`UseOfMfc=Dynamic` and its `stdafx.h` pulls in `afxwin.h`. Every other MFC harness in the tree
declares one. `AlexTest/README.md:195-199` explains why that global was thought to matter:

> `TargetCore.dll` is an **MFC Extension DLL**. Its `DllMain` calls `AfxInitExtensionModule` /
> `new CDynLinkLibrary(...)`, which requires MFC's thread state (`AfxGetThread()`) to be
> valid. That state is set up by the `CWinApp theApp` global — which is constructed *after*
> implicit DLLs would normally load.

This was tested rather than assumed. **The omission is benign on Windows.** All four builds
compile and link with no warnings or errors, and the two-process handshake completes green in
both configurations (see the results below). The property that actually matters is the
`/DELAYLOAD` — deferring the DLL load until the first API call inside `main()` is sufficient
on its own, and the `CWinApp` global is not required for the extension DLL to initialise.

---

## Verified build and run results

Toolchain: MSBuild 17.14.51.32402, Visual Studio 2022 Community, `v143`, x64.
These results predate the move to Visual Studio 2026 / `v145` and have NOT been
re-measured on it. The project files in this repository now build at `v145`, so
the table below records what a v143 build did, not what the current one does.
Kernel DLLs staged from the prebuilt `MSCS\bin\Debug64` and `MSCS\bin\Release64`.

### Builds — 4 of 4 clean

| Project | Configuration | Result |
|---|---|---|
| `AlexInterop` | `Debug\|x64` | clean → `out\x64\Debug\AlexInterop.exe` |
| `AlexInterop` | `Release\|x64` | clean → `out\x64\Release\AlexInterop.exe` |
| `AlexTest` | `Debug\|x64` | clean → `out\x64\Debug\AlexTest.exe` |
| `AlexTest` | `Release\|x64` | clean → `out\x64\Release\AlexTest.exe` |

No warnings, no errors; the post-build DLL staging succeeded in every case.

### Runtime — `AlexInterop`, both configurations PASS

Server started first, client two seconds later, both against `127.0.0.1`
(Debug on port 7811, Release on port 7813).

| Configuration | Server exit | Client exit | Verdict |
|---|---|---|---|
| `Debug\|x64` | **0** | **0** | PASS |
| `Release\|x64` | **0** | **0** | PASS |

No assertion fired, no modal dialog, no delay-load fault, no hang. Debug server transcript:

```
=== alex_test (two-process) — SERVER ===
Port : 7811  IP : 127.0.0.1(listen)  pid : 13472

[pid=13472 SERVER] hub thread started
[pid=13472 SERVER] service connection posted (bind INADDR_ANY + listen)
[pid=13472 SERVER] waiting up to 15s for the client's BCast...
[pid=13472 SERVER] On_ConStartup    con=0000021366035A60 addr='AlexTest.Client'
[pid=13472 SERVER] On_ConListen     con=0000021366035A60 addr='AlexTest.Client'
[pid=13472 SERVER] On_ConAccept     con=0000021366035A60 addr='AlexTest.Client'
[pid=13472 SERVER] On_ConAccept     con=0000021366040170 addr='AlexTest.Client'
[pid=13472 SERVER] On_ConLogin      con=0000021366040170 addr='AlexTest.Client'

[SERVER] BCast from 'AlexTest.Client':
  > HelloFromInterop

[pid=13472 SERVER] SUCCESS - received the client's BCast over TCP
[pid=13472 SERVER] shutdown begin
Done (SERVER exit=0).
```

and the matching client:

```
[pid=1328 CLIENT] On_ConStartup    con=0000020880D29430 addr='AlexTest.Server'
[pid=1328 CLIENT] On_ConConnect    con=0000020880D29430 addr='AlexTest.Server'
[pid=1328 CLIENT] On_ConLoginAck   con=0000020880D29430 addr='AlexTest.Server'
[CLIENT] Login ack from 'AlexTest.Server' - TCP connection ready.
[CLIENT] Posted BCast: "HelloFromInterop"
[pid=1328 CLIENT] SUCCESS - handshake completed, BCast posted
[pid=1328 CLIENT] On_ConClose      con=0000020880D29430 addr='AlexTest.Server'
Done (CLIENT exit=0).
```

The Release run is identical in sequence, differing only in pids, con pointers and port.

Two details in the server trace are worth noting, as they are visible in both configurations
and are normal for this transport:

- `On_ConAccept` fires **twice**, on two different con pointers. This is the listening con
  handing off to the accepted session con — the same accept-spawn swap the pipe transports
  perform. The subsequent `On_ConLogin` arrives on the second (session) con.
- The server never emits `On_ConLoginAck`; that leg is the client's, which is why the client
  is the side that posts.

`AlexTest` itself was built but not run through a full handshake here — its server blocks on
`getchar()` and needs an interactive Enter, which is the very limitation `AlexInterop`
exists to remove.

---

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](../LICENSE) for the full text.

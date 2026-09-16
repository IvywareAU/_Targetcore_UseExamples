# FacadeExamples

Every harness in [`DirectExamples`](../DirectExamples), rewritten on top of
**[TargetFacade](../../TargetFacade)** instead of Targetcore directly.

Same questions, same verdicts, same exit-code contract — so the two trees can be
read side by side. What changes is how much you have to know to ask the question.

```
msbuild "FacadeExamples(2026).sln" -p:Configuration=Debug -p:Platform=x64
.\run_all.ps1                       # build + run everything, summarise
.\run_all.ps1 -Config Release
```

**Status: 13/13 on Debug|x64 and 13/13 on Release|x64** (11 harnesses; the two
two-process ones count their server and client separately), measured 2026-08-13
against a freshly rebuilt kernel.

`RouteLoopbackTestLight` briefly regressed against that kernel and is green
again — the cause and the fix are worth reading before you rely on tree routing:
see [downward relay](#downward-relay-and-security_review-m2) below.

This tree also carries an **intermittent access violation during process
teardown** in `AlexTestLight (client)` and `AlexInteropLight (client)` on
Release — raised *after* the harness printed `Done (exit=0)`, so the test itself
completed and its verdict stands. Measured at 2-3 runs in 6, so a given run may
show 12/13 or 10/13; the run recorded above happened not to trip it. **It
predates the ABI-4 migration below**: the unmodified pre-migration sources
against the pre-migration facade fault at the same rate. Not diagnosed; it is a
facade/kernel teardown fault, not a harness bug.

## Downward relay and SECURITY_REVIEW M2

For a window, `RouteLoopbackTestLight` scenario **[2]** — `Root → Leaf`, straight
down the tree through A — did not arrive, and the receiving end said why:

```
[ERROR] []P2PeerCon::GateAppMsgInbound(this=)
Message source [RouteMesh.Root] is not the logged-in identity [RouteMesh.Root.A]
ADVICE  : Connection dropped out
```

`P2PeerCon::GateAppMsgInbound` (`P2PeerCon.cpp:2015`) is the SECURITY_REVIEW
**M2 source binding**: the source address is read off the wire, so without a
check a peer that logged in as one identity could attribute its messages to
another — and the source is what routing, the Explorer registry and every
application handler key on. The rule was that a message's source must be the
peer's own address **or a descendant of it** (`IsRable()` — exact match or a
hop-boundary prefix).

That rule is right for a link to a **descendant** and cannot hold on a link to an
**ancestor**, because an ancestor is this hub's gateway to the rest of the tree:
everything routed down arrives from it still carrying its original source, which
is outside its subtree whenever the message came from another branch. The effect
was asymmetric, which is why only one of the four scenarios failed:

| leg | receiving hub's peer | source | then |
|---|---|---|---|
| **[1]** `Leaf → B`, up then down | B's peer is `Root` | `Root.A.Leaf` | passed — source *below* Root |
| **[2]** `Root → Leaf`, down via A | Leaf's peer is `Root.A` | `Root` | **refused** — source *above* A |

**Fixed** (`P2PeerCon.cpp`, 2026-08-13): an **ancestor link is admitted without a
source check**. Links to a descendant and to an unrelated peer are completely
unchanged, and those are the forgery cases the rule was written for. What the fix
grants is narrow but should be stated plainly: an ancestor may now also present a
source from outside its own subtree. It could already present anything *inside*
its subtree — which includes this hub and every descendant of it — so "a parent
can speak as its own children" was true before the fix and still is.

Binding the source on an ancestor link cannot be done from the address alone: it
needs to know which branch a source is reachable through, which is routing state
this gate does not have. Tightening it is the PSK / route-attestation work, not a
predicate change. See `SECURITY.md`.

## Note on Targetcore's flat C API

The numbers above were taken after `Targetcore_c.{h,cpp,_u8.cpp}` — the flat
`extern "C"` / Panama surface — was removed from `Targetcore.dll` on 2026-08-13.
Nothing here used it: `dumpbin /imports` shows `TargetFacade.dll` importing
**zero** of its 74 symbols, and removing it changed no result in this tree. The
sources are preserved in `MSCS_JavaBindings\Targetcore\native\`.

## Migrated to TargetFacade ABI 4

The facade collapsed its eight typed arming verbs onto **one pair**, so every
harness here changed one line per arming call:

```cpp
server.listen      ( peer, 7801 );                    // was
server.listen      ( peer, L"tcp://:7801" );          // is

client.connectPipe ( peer, kPipeName );               // was
client.connect     ( peer, L"pipe://" kPipeName );    // is
```

`listenPipe`/`connectPipe`, `listenDmx`/`connectDmx` and `listenSerial`/
`connectSerial` are gone; the transport is a *value* in the endpoint string now.
`common\LightHarness.h` grew five one-line composers (`light::TcpListen`,
`TcpDial`, `Pipe`, `Dmx`, `Serial`) so the harnesses keep their typed constants,
and `light::HrName` learned the five newer facade HRESULTs.

Two consequences worth knowing before you read a log:

* **`ABI_VERSION` is 4 and the facade accepts only 4.** Removing vtable slots
  moved everything after them, so there is no compatible prefix — these
  harnesses must be rebuilt against the matching DLL, or they fail cleanly at
  `P2PF_CreateNetwork` with `P2PF_E_ABI_MISMATCH`.
* **Every `X.Server` / `X.Client` pair is a SIBLING pair**, and the surviving
  verbs classify the pair where the typed ones did not. So most harnesses now
  log one extra line at startup — *"'…Server' and peer '…Client' are neither
  ancestor nor descendant"* — and the arming call returns
  `P2PF_S_UNRELATED_LINK`, which is a **success** code (`FAILED()` does not see
  it, and every check here uses `FAILED()`). The addresses were deliberately
  **not** re-based to parent/child: this tree exists to be read side by side
  with `DirectExamples`, which uses exactly these names. `RouteLoopbackTest`
  is the one harness whose addresses are already hierarchical, and it stays
  silent.

## The map

| Original | Light | What the Light version does differently |
|---|---|---|
| `WsaMeshTest` | ✅ same | `listen` / `connect` over `tcp://` |
| `PipeMeshTest` | ✅ same | the same two verbs, one string changed to `pipe://` |
| `DmxMeshTest` | ✅ same | …and to `dmx://` |
| `Com232MeshTest` | ✅ same | …and to `serial://COMn` |
| `PipeMsgMapTest` | ✅ same | `onTopic(name, λ)` replaces `BEGIN_P2PeerMsg_MAP` / `ON_P2PeerMsg` |
| `PipeMsgFactoryTest` | ⚠️ behaviour kept, mechanism gone | the facade has no factory family; one `send()` verb replaces `RedirectFactory` / `ResponseFactory` |
| `TwoConTest` | ✅ same | the duplicate-peer rule is a named error, `P2PF_E_CON_DUPLICATE` |
| `LocalInMemoryTest` | ⚠️ result kept, mechanism gone | no `PostP2Pmsg` pump injection in the facade; uses Dmx instead |
| `AlexTest` | ✅ same, one fix | server no longer dies on redirected stdin (see below) |
| `AlexInterop` | ⚠️ **loses its whole point** | the original exists to be **Linux-portable**; the facade is Windows/MFC |
| `RouteLoopbackTest` | ⚠️ different framework | the original is **not an MSCS harness at all** |

### The four that are not clean translations

**`LocalInMemoryTest`** used `PostP2Pmsg(pMsg, targetHub.GetHubID())` — dropping
a message straight onto another hub's pump queue with no connection, no
handshake, and a documented rule that it is only legal from a non-hub thread.
The facade's model is that hubs talk over connections, so there is no
`GetHubID()` and no raw pump queue. The Light version reaches the same end state
(two in-process hubs, messages both ways, nothing leaves the address space) over
the Dmx transport, and pays one login handshake for it. **If you need pump
injection, that is a reason to use Targetcore directly.**

**`PipeMsgFactoryTest`** existed to show that `RedirectFactory` produces
something that routes like a hand-built `P2PeerMsg32`, and carried a long
warning about why *not* `ResponseFactory` (it inherits the request's routing
prefix, so standalone-posting one loops the reply back into the sender's own
map). With one `send(dest, topic, bytes, size)` verb and no envelope to inherit,
that entire class of decision disappears. The Light version keeps what was
really being demonstrated — a caller-built binary payload routed to a named
handler — and drops the navigation of an API that no longer exists.

**`AlexInterop`** is the Linux port's Phase-3 exit criterion ("AlexTest green
Linux↔Linux"). It deliberately avoids every Win32-ism so the same source
compiles against the io_uring shim with g++. TargetFacade is a Windows MFC DLL,
so a facade rewrite **forfeits the one property the original was built for.** The
Light version is here for completeness of the mapping, not as a replacement — if
you are working the Linux port, use the original.

**`RouteLoopbackTest`** is the odd one out in the *original* tree: it drives
treehub_runtime's `PeerNetwork::route()`, the clean-room in-process router the
code generator targets — no `P2PeerHub`, no `PostP2Pmsg`, no pump thread. There
is no Targetcore in it to put a facade over. So the Light version asks the
original's *question* of the real kernel instead: build the same four-node tree
out of facade hubs and see how MSCS routes it. It does, and the answers are
worth having — see below.

## What the rewrite cost, in lines

Non-comment, non-blank lines of everything in the project directory
(`.cpp` + `.h`, so `stdafx.h` and friends count — because carrying them was part
of the job):

| | original | light |
|---|---:|---:|
| all eleven | **2376** | **930** |
| shared harness helper | — | 106 (`common\LightHarness.h`) |

Per harness the typical drop is ~220 → ~65 lines. `RouteLoopbackTest` is the one
that grew (110 → 137), because the Light version tests four scenarios against a
real four-hub mesh rather than three against an in-process function call.

What actually disappeared, in every single file:

* `CWinApp theApp;` and a `stdafx.h` pulling in `afx.h`, `afxwin.h`, `afxext.h`,
  `afxmt.h`, `afxtempl.h`, `WinSock2.h`, `mswsock.h` and four Targetcore headers
* the `_CrtSetReportHook` assert trap every harness needed, because a debug
  ASSERT inside the kernel pops a **modal dialog** and hangs a headless run
* `StartupP2Pmsg` / `WSAStartup` / `SpawnHub` / `CloseHub` / `WaitForSingleObject`
  / `CloseHandle` / `CleanupP2Pmsg` / `WSACleanup`, in that order, on *every*
  exit path — six or more returns per file
* a `P2PeerHub` subclass with up to seven `On_Con*` overrides whose only job was
  to print how far the handshake got
* hand-built `new P2PeerMsg32(src, dst, name, data, size)` and the ownership
  handoff to `PostP2PeerMsg`

## Three things the rewrite proved

**1. Serial actually works.** The facade README listed the serial path as
"implemented but untested — needs two real or virtual COM ports". This box has a
com0com COM5↔COM6 pair, so `Com232MeshTest` (Light) is the first end-to-end
exercise of it: **PASS**, both configurations. It is also the one transport
`IP2PNetwork::Link` cannot express, because each side names its *own* port.

**2. The kernel really does route a tree.** `RouteLoopbackTest` (Light) builds
Root / A / B / Leaf over Dmx and confirms, at runtime:

* `Leaf → B` is delivered **across two hops** (up to the root, then down) even
  though the two hubs share no connection;
* `Root → Leaf` is delivered straight down through A;
* an address nobody holds is delivered **nowhere**, and the kernel says so
  (`Bad destination address`);
* a broadcast from the root reaches **every descendant, including the grandchild
  Leaf** — the tree-shaped behaviour the generated router does not have.

The two downward results — `Root → Leaf` and the broadcast reaching Leaf — were
briefly lost to the M2 source binding and restored on 2026-08-13. See
[Downward relay and SECURITY_REVIEW M2](#downward-relay-and-security_review-m2).

**3. There is still an ordering rule — just not the one the originals had.**
Learned by writing `LocalInMemoryTest` (Light), and it cost a debugging cycle:

> `onPeerUp` on the **listening** side can fire **before the dialling side has
> finished logging in**. Sending from it races the handshake, and the far end
> answers an early message with *"Application message received before login /
> Connection dropped out"* — it drops the whole connection.

So the first message on a link must come from the side that **dialled** (its
peer-up means login-ack, which is strictly later); the listening side should
answer on receipt rather than announce itself. Every harness here follows that
shape.

The originals' own ordering hazards are gone, though: facade TCP and pipe dials
**retry** (~2/s), so the `Sleep(750)` every original needed between arming a
listener and dialling it is unnecessary — `WsaMeshTest` (Light) completes its
whole round trip in ~17 ms. Dmx and serial dials still do not retry (a missing
in-process service or COM port is a configuration fault, not a timing one), so
those harnesses arm the listener first, and say so.

## One deliberate behaviour change

`AlexTest`'s original server blocked on `getchar()`, which cannot be run
unattended: with stdin redirected it returns EOF immediately and the "server"
exits before the client can reach it. (The original carried a comment about
exactly this.) The Light server waits on the message, and *additionally* on a
keypress **only when stdin is a real console** (`GetConsoleMode` succeeds). It
still stops on Enter when you run it by hand, and `run_all.ps1` can now drive
both processes.

## Layout

```
common\Light.props        every build setting, shared: one include dir, one
                          import lib, DLL staging. That is all a client needs.
common\LightHarness.h     log / gate / verdict / endpoint helpers (106 lines)
<Harness>\<Harness>.cpp   one file per harness, no stdafx, no MFC
bin\<Config>\             all exes plus the three staged DLLs
run_all.ps1               build + run + summarise
```

All eleven build into one output directory, so the facade and kernel DLLs are
staged once and every harness finds them.

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](../LICENSE) for
the full text.

# P2P MSCS Architecture FAQ

Questions that came up while reading `DmxMeshTest`, answered from the source — and then
again, once the same harness existed three times over.

**Conventions.** File references are relative to the MSCS root (`TargetCore/P2Pwin32.cpp`,
`_TargetCore_UseExamples/DirectExamples/DmxMeshTest/DmxMeshTest.cpp`). Line numbers were accurate at the time of
writing and will drift — treat them as a starting point, and search for the quoted code if a
reference misses.

---

## The five trees

The same eleven harnesses now exist five times, over five different surfaces of one
framework — the three C++ ones this document was first written about, and two more that
reach the same surfaces from another runtime. Every question in Part I is about the kernel
and is true underneath all five; Part II is about what each layer changes.

| Tree | Written against | Links | Finds the implementation |
| ---- | --------------- | ----- | ------------------------ |
| [`DirectExamples`](DirectExamples) | **TargetCore** directly — `P2PeerHub`, `P2PeerCon`, `P2PeerMsg`, MFC | `TargetCore.lib` + `Msgcore.lib` | link time |
| [`FacadeExamples`](FacadeExamples) | **TargetFacade** — one header, flat vtable ABI, HRESULTs | `TargetFacade.lib` | link time |
| [`ComExamples`](ComExamples) | **TargetCom** — ATL dual interfaces, connection-point events | *nothing of MSCS* — `ole32`/`oleaut32`/`uuid` | **the registry, at run time** |
| [`dotNetExamples`](dotNetExamples) | the same **TargetCom**, from C# | *nothing of MSCS* — a CCW per object | the registry, at run time |
| [`PanamaJavaExamples`](PanamaJavaExamples) | the same **TargetFacade**, from Java | *nothing at all* — Panama reads the raw vtables | `LoadLibrary`, at run time |

They are stacked, not parallel: `TargetCom` is a COM layer over `TargetFacade`, which is a
facade over `TargetCore`. A message sent from the COM tree traverses all three.

```
ComExamples, dotNetExamples      ──> TargetCom.dll      (BSTR / VARIANT / IDispatch)
FacadeExamples, PanamaJavaExamples ─> TargetFacade.dll  (flat vtable, HRESULT)
DirectExamples                   ──> TargetCore.dll ──> Msgcore.dll
```

Each tree carries its own `README.md` with the harness-by-harness mapping and its test
status. All five keep the same exit-code contract, so the same harness can be compared
directly across them: `0` success, `1` setup failure, `3` timeout / expectation not met.
(`2` used to mean "an MFC/CRT assertion fired"; see [Q18](#18-what-happens-to-errors-exceptions-and-asserts).)

---

## Contents

**Part I — the kernel.** True in all three trees, whether or not you can see it.

1. [Why does the example call `WSAStartup` when there is no socket?](#1-why-does-the-example-call-wsastartup-when-there-is-no-socket)
2. [What does `StartupP2Pmsg(16)` do, and why 16?](#2-what-does-startupp2pmsg16-do-and-why-16)
3. [Hubs vs pumps — what is the difference?](#3-hubs-vs-pumps--what-is-the-difference)
4. [Why does each hub need its own thread?](#4-why-does-each-hub-need-its-own-thread)
5. [Is it one thread per pump?](#5-is-it-one-thread-per-pump)
6. [So a hub thread holds both a hub and a pump?](#6-so-a-hub-thread-holds-both-a-hub-and-a-pump)
7. [What happens if a pump thread throws an exception?](#7-what-happens-if-a-pump-thread-throws-an-exception)
8. [Hub map vs pump map — and is the sender the same thread as the receiver?](#8-hub-map-vs-pump-map--and-is-the-sender-the-same-thread-as-the-receiver)
9. [Where does the login handshake actually run?](#9-where-does-the-login-handshake-actually-run)
10. [How many pumps does a hub really have?](#10-how-many-pumps-does-a-hub-really-have)

**Part II — the layers.** What the facade and the COM layer change, hide, or cannot do.

11. [Which tree should I write against?](#11-which-tree-should-i-write-against)
12. [What happened to the startup and shutdown boilerplate?](#12-what-happened-to-the-startup-and-shutdown-boilerplate)
13. [Where did `BEGIN_P2PeerMsg_MAP` go?](#13-where-did-begin_p2peermsg_map-go)
14. [Which thread runs my callback?](#14-which-thread-runs-my-callback)
15. [Does arming order still matter?](#15-does-arming-order-still-matter)
16. [Why must the side that dialled speak first?](#16-why-must-the-side-that-dialled-speak-first)
17. [What can the layers *not* do?](#17-what-can-the-layers-not-do)
18. [What happens to errors, exceptions and asserts?](#18-what-happens-to-errors-exceptions-and-asserts)
19. [How do events reach a COM client — and why can't PowerShell sink them?](#19-how-do-events-reach-a-com-client--and-why-cant-powershell-sink-them)

---

## 1. Why does the example call `WSAStartup` when there is no socket?

```cpp
WSADATA oWsaData;
WSAStartup(MAKEWORD(2, 2), &oWsaData);      // DmxMeshTest.cpp:192
```

**It is boilerplate, and this particular test does not need it.**

`StartupP2Pmsg()` never touches Winsock. The only place the core initialises it for you is
`P2PeerService::Run()` (`TargetCore/P2PeerService.cpp:678`), and a directly-driven hub never
goes through that. Hence the standard block documented in `TargetCore/examples.md:131-137`:

```cpp
if (!StartupP2Pmsg(16)) { /* fatal */ }
// StartupP2Pmsg() does NOT call WSAStartup — when you drive a hub directly
// (rather than via P2PeerService::Run) you must init Winsock yourself.
// Required for P2PeerConWsa; harmless for P2PeerConPipe.
```

Every harness in `DirectExamples` carries the same two lines, and `DmxMeshTest` was written
as a copy of `PipeMeshTest`.

Is it needed for Dmx? No. `P2PeerConDmx.cpp/.h` and `P2PeerioDmx.cpp` contain no `socket()`,
no `SOCKET` member, no `htons`/`inet_*`. The only `WSA` token there is `WSAGROUP(hr)`
(`P2PeerConDmx.cpp:324`), an error-category tag for the exception reporter that does not
require an initialised Winsock. The one live `gethostname()` in the core
(`P2Pwin32.cpp:1358`) sits in `NotifyP2PmsgExp_Hub`, reachable only from a P2Pexplorer pump
context, which this harness never creates.

**Reasons to keep it anyway:** it is refcounted and cheap, correctly paired with
`WSACleanup()` (`DmxMeshTest.cpp:248`), keeps the startup block identical across all the mesh
examples so the transport is the only diff, and keeps the harness working if a
`P2PeerConWsa` is ever mixed into the mesh.

> **In the Light and COM trees:** the question does not arise. `FacadeNetwork` calls
> `WSAStartup(2,2)` once when the first network reference is taken and `WSACleanup()` when
> the last is dropped (`TargetFacade/src/FacadeNetwork.cpp:67`), so a client never sees
> Winsock at all — the Light harnesses do not `#include <WinSock2.h>` and the COM ones do not
> link `ws2_32.lib`. The refcounting means it is still correct if a process holds several
> networks, which is the same reason the original kept the "unnecessary" call.

---

## 2. What does `StartupP2Pmsg(16)` do, and why 16?

`StartupP2Pmsg()` (`P2Pwin32.cpp:1631`) is the process-wide one-time boot of the P2Pmsg
kernel. Nothing else in the framework is legal before it. In order:

1. `StartupP2Pevent()` — brings up the event subsystem.
2. **Re-entry guard** — throws "P2Pmsg environment already started" if `s_apP2PmsgHubMgr` is
   already allocated. One live environment per process; `CleanupP2Pmsg()` before restarting.
3. **Range check** — `nMaxHubs` must be `1..MAX_P2PmsgHub`, where `MAX_P2PmsgHub = 256`
   (`P2Peer.h:53`).
4. **Creates the three global critical sections** the kernel serialises on:
   `s_oCSectionP2Pmsg`, `s_oCSectionP2PmsgHub`, `s_oCSectionP2PmsgPump`.
5. **Allocates the hub table** — `new P2PmsgHubMgr*[nMaxHubs+7]`, zero-filled, and records
   `s_uxP2PmsgHubMgr = nMaxHubs`. Hub IDs are 1-based (`CleanupP2Pmsg` walks
   `nHub = 1 … s_uxP2PmsgHubMgr`), so slot 0 is dead and the `+7` is defensive slack.
6. `StartupP2PmsgSink()` — idempotent sink-environment init (`:6232`); its `TRUE` becomes the
   return value.

It allocates no threads and opens no handles. Threads and IOCPs come later, per hub, in
`CreateP2PmsgHub`.

### What the argument caps

`nMaxHubs` is the **hard ceiling on concurrent hubs in this process** — not pumps, not
threads. Enforced in exactly one place (`P2Pwin32.cpp:1759`):

```cpp
if ( s_ThreadID_P2PmsgHub.GetCount() >= (INT_PTR)s_uxP2PmsgHubMgr )
   EVERR->MODULE->Message("Attempt to exceed configured hub limit (%i)", ...)->Throw();
```

> **Doc bug:** `TargetCore/examples.md` glosses the argument as "pump/thread pool hint". That
> is wrong — it has nothing to do with pumps or a thread pool.

### Why 16

No reason specific to any test. The header default is different — `P2Pwin32.h:33` declares
`StartupP2Pmsg(UINT nMaxHubs = 64)` — but every hand-written harness in the tree passes 16
(`dmx_mesh.cpp:156`, `mix_con.cpp:179`, `MixConTest`, `Com232MeshTest`, the `dsp_*` tests,
~30 call sites, all `16`). A copied convention, not a tuned figure.

`DmxMeshTest` creates exactly two hubs, so anything >= 2 works. The only cost of the number
is the array: `16+7 = 23` pointers, roughly 184 bytes on x64. Nothing else scales with it.

The copied convention has since hardened into a real limit: `TargetFacade` passes the same
`16`, and a facade or COM client has no way to ask for more. See the callout at the end of
this answer.

### The `if (!...)` guard is nearly dead code

Every real failure inside goes out via `EVERR->…->Throw()`, and `StartupP2PmsgSink()` returns
`TRUE` unconditionally once up (see the comment at `:6234`, which documents a fix for exactly
the case where a re-start reported a spurious `FALSE`). So the reachable `return FALSE` is
effectively empty, and `main()` has no `try`/`catch` around the call — a genuine failure
unwinds as an unhandled `Msgexception` rather than printing the `"FATAL: StartupP2Pmsg()
failed."` line. Same pattern in all the sibling examples.

> **In the Light and COM trees — and this one is a real constraint, not just a hidden
> detail.** `FacadeNetwork` hardcodes the same figure (`FacadeNetwork.cpp:62`):
>
> ```cpp
> m_bMsgUp = StartupP2Pmsg ( 16 );
> ```
>
> and the public API exposes no way to change it. **A facade or COM client is therefore
> capped at 16 hubs per process**, not the header's default of 64 and not `MAX_P2PmsgHub`'s
> 256. Nothing in either example tree comes close — `RouteLoopbackTest` builds the largest
> mesh at four hubs — but a client that wants more has to change the facade, not its own
> code. The 17th `createHub` / `CreateHub` would fail with `P2PF_E_HUB_SPAWN`.
>
> The upside of the same line: the failure that "unwinds as an unhandled `Msgexception`"
> above cannot escape a facade client. `P2PF_CreateNetwork` catches it and returns
> `P2PF_E_STARTUP`, which is why the Light harnesses have no `try`/`catch` around startup
> either — and, unlike the originals, do not need one.

---

## 3. Hubs vs pumps — what is the difference?

**A hub is a thread.** `P2PmsgHubMgr`'s constructor (`P2Pwin32.cpp:745`):

```cpp
m_nHubID = GetCurrentThreadId();
s_ThreadID_P2PmsgHub.SetAt ( m_nHubID, this );
```

The `P2PmsgHubID` *is* the Win32 thread ID of whichever thread created it. `SpawnHub` says so
explicitly (`P2PeerHub.cpp:166`): "the allocated win32 threadID becomes the P2PmsgHubID". A
hub owns the mesh-visible identity — the `P2Paddr`, an IOCP, the sink map, the connection
list, and a list of pumps.

**A pump is also a thread** — the message-processing loop. `P2PmsgPump` ctor (`:211`):

```cpp
m_nThreadId = GetCurrentThreadId();
m_nPumpID   = GetCurrentThreadId();
s_ThreadID_P2PmsgPump.SetAt( m_nThreadId, this );
```

Enforced one-per-thread: `CreateP2PmsgPump` throws "ThreadID=%i already has P2PmsgPump
context" (`:2381`). Each pump carries its own queue event, `OVERLAPPED`, IOCP handle and
target.

### The relationship

Strict containment, 1 hub : N pumps, and the hub always has at least one:

```cpp
Factory ( UINT nPumpsMax )                                  // P2Pwin32.cpp:886
{
  pMgr->m_apP2PmsgPump = new P2PmsgPump* [nPumpsMax+7];
  pMgr->m_nPumpsMax    = nPumpsMax;
  pMgr->CreateP2PmsgPump ( );                               // pump #0, on the hub's own thread
}
```

That first pump lands on the hub's thread, which is why `CreateP2PmsgHub` can immediately do
`s_ThreadID_P2PmsgPump.Lookup(GetCurrentThreadId(), pP2PmsgPump)` (`:1777`) and find one to
hang the IOCP off.

The hub is the **addressable node**; pumps are the **execution slots inside it**. Connections
attach to a specific pump — the second parameter of
`PostP2PeerCon(P2PeerCon* pCon, P2PumpID nPumpID = 0)` (`P2PeerHub.h:78`), defaulting to
pump #0.

> See [Q10](#10-how-many-pumps-does-a-hub-really-have) — in practice N is always 1 in this
> codebase.

### The two limits are different scopes

|                | `nMaxHubs`                            | `nPumpsMax`                                     |
| -------------- | ------------------------------------- | ----------------------------------------------- |
| Scope          | whole process                         | one hub                                          |
| Set by         | `StartupP2Pmsg(16)`                   | `CreateHub(addr, nPumpsMax)` / `SpawnHub`        |
| Stored in      | `s_uxP2PmsgHubMgr` (global)           | `m_nPumpsMax` (per `P2PmsgHubMgr`)               |
| Checked at     | `P2Pwin32.cpp:1759`                   | `P2Pwin32.cpp:2400`                              |
| Error          | "Attempt to exceed configured hub limit" | "Attempt to exceed configured pump limit (%i) for hub" |

`MAX_P2PmsgHub` and `MAX_P2PmsgPump` (both `256`, `P2Peer.h:53,55`) are neither of those —
they are compile-time caps on what you may *request*. You cannot raise them without
recompiling.

### In DmxMeshTest

Two hubs, `oServer` and `oClient`, each created by `SpawnHub()` — 2 of 16 hub slots, on two
dedicated threads. Neither passes a pump count, because `SpawnHub` supplies it
(`P2PeerHub.cpp:163`):

```cpp
oContext.nPumpsMax = HubPumpsMaxKnob ( );   // W8: env-tunable, default 15
```

That is the `P2PMSG_PUMPS_MAX` env knob, clamped to `[15, 256]` (`P2PeerHub.cpp:45-59`). Each
hub gets 15 pump slots and uses exactly 1. Total: 2 hub threads for the whole test.

**More hubs != more concurrency for one endpoint.** A hub is an addressable node in the mesh.

> **In the Light and COM trees:** identical, because the facade does not reimplement any of
> this — `FacadeHub` *derives from* `P2PeerHub` and calls `SpawnHub()`
> (`TargetFacade/src/FacadeHub.cpp:149`). So:
>
> * one `p2pf::Hub` **is** one `P2PeerHub` **is** one thread hosting pump #0;
> * `DmxMeshTest` (Light) has exactly the same two hub threads as the original;
> * everything in Q3–Q6 about thread identity, `m_nHubID == GetCurrentThreadId()` and the
>   one-pump-per-thread rule is still literally true underneath — you just have no API to
>   observe it, because `Address()` returns the `P2Paddr` and nothing returns a HubID.
>
> **The COM tree adds exactly one thread per hub**: `CP2PHubCom::Init` starts a dedicated MTA
> dispatch thread before creating the kernel hub (`TargetFacade/com/ComHub.cpp`, the
> `_beginthreadex` in `Init`). A COM hub is therefore **two** threads — the kernel pump, plus
> the dispatch thread that replays its callbacks into your apartment. Why that second thread
> has to exist is [Q14](#14-which-thread-runs-my-callback).

---

## 4. Why does each hub need its own thread?

Strictly it does not need its *own* thread — it needs to **own a thread's context** for its
lifetime. Three things force that.

### 4.1 Identity is derived from the thread

Because `m_nHubID = GetCurrentThreadId()`, the whole framework resolves "which hub/pump am I
in?" by looking up the *calling thread*. There is no context handle threaded through the API.
`GetP2PmsgHubID()` (`:1801`), `PumpP2Pmsg()` (`:4381`), `QueryP2PmsgExp_Hub()` (`:1385`) all
do a `Lookup(GetCurrentThreadId(), …)` and throw if it misses:

```cpp
s_ThreadID_P2PmsgPump.Lookup(GetCurrentThreadId(),pP2PmsgPump);
if ( !pP2PmsgPump )
  EVERR->…->Message("P2Pmsg pump not started")->Throw();
```

Hence the invariant asserted on entry and every loop iteration (`P2PeerHub.cpp:372`, `:418`):

```cpp
ASSERT(m_nHubID==GetCurrentThreadId());
```

Two hubs on one thread would collide on the same map key — the second `SetAt` displaces the
first, and every ambient lookup silently resolves to the wrong hub. The design trades
explicit context-passing for thread affinity, so thread affinity becomes mandatory.

### 4.2 The pump loop blocks

`RunHub` is a blocking wait loop (`P2PeerHub.cpp:383`):

```cpp
while ( (dwResult=PumpP2Pmsg(dwMSec,nSigID)) != 0 )   // dwMSec = 8000
```

`PumpP2Pmsg` polls timers, drains the FIFO under the pump's `m_oCSection`, and otherwise
parks on the pump's event or IOCP until timeout. There is no cooperative yield point that
would let a second hub's loop interleave.

### 4.3 Isolation is the payoff

`ProcHub` wraps the whole hub lifecycle in `try`/`catch` (`P2PeerHub.cpp:330-348`) with an
explicit note that this matters *because* it is a thread proc — an escaping exception calls
`std::terminate` and kills the process. A per-hub thread lets a hub fault, drain, pause
(`P2PsigHub_PAUSE`) or close (`P2PsigHub_CLOSE`) without touching its neighbour, and keeps a
slow user callback in one hub from stalling the other's pump.

### The escape hatch

`CreateHub()` exists for when you do not want a spawned thread (`P2PeerHub.cpp:190-193`):

> Creates P2PmsgHub within the context of this thread … Facilitates integration of P2PeerHub
> processing in 3rd Party environments such as MFC or service thread

That is what `P2PeerService::Run()` uses in service mode (`P2PeerService.cpp:692-696`) —
`CreateHub()` then `RunHub()` on the service thread, which "locks up this processing
context". Console mode spawns instead. Either way it is one thread per hub; `CreateHub` just
lets you nominate which one.

### Why DmxMeshTest genuinely needs two threads

The Dmx handshake is a real rendezvous: the server hub must be pumping `Listen()`/`Accept()`
at the moment the client hub calls `Connect()`. That is what the `Sleep(750)` at
`DmxMeshTest.cpp:208` buys:

```cpp
// Let the server pump run Listen()/Accept() so the client's Connect()
// finds a listening service con in the global Dmx registry.
```

On a single thread this deadlocks — the accept side never runs while the connect side is
blocked, the login handshake never completes, and the harness exits 3 (TIMEOUT).

---

## 5. Is it one thread per pump?

Yes, and it is structurally enforced in both directions.

- **<= 1 pump per thread** — the throw in `CreateP2PmsgPump` (`P2Pwin32.cpp:2380`):

  ```cpp
  if ( s_ThreadID_P2PmsgPump.Lookup(GetCurrentThreadId(),pP2PmsgPump) || pP2PmsgPump )
    EVERR->…->Message("ThreadID=%i already has P2PmsgPump context", GetCurrentThreadId())->Throw();
  ```

- **Exactly 1 thread per pump** — by construction: `m_nPumpID = m_nThreadId =
  GetCurrentThreadId()`, and the pump is only reachable through `s_ThreadID_P2PmsgPump` keyed
  by that ID.

Neither direction can be violated, because the ID *is* the thread — there is no separate
identifier space that could alias.

**But pump #0 is not an extra thread.** `Factory()` creates it inline on whatever thread is
building the hub (`:889`), so a hub with one pump is one thread total, not two.

**Extra pumps each get a `CreateThread`** — `P2PeerTarget::SpawnPump`
(`P2PeerTarget.cpp:136`), the pump-level mirror of `SpawnHub`:

```cpp
HANDLE hThread = CreateThread ( 0, 0, pfnThreadProc, &oContext, 0, pnP2PumpID );
…
while ( !P2PmsgPumpExists(*pnP2PumpID) )   // spin until the pump registers
  YieldForP2PmsgPump ( uSpins );
```

`pnP2PumpID` is `CreateThread`'s thread-ID out-param — the ID the OS returns *is* the PumpID.
The new thread runs `ProcPump` (`:206`), which calls `CreateP2PmsgPump(pTarget->GetHubID(), …)`
to attach itself to the hub, then loops in `RunPump()`.

### The pump thread is the I/O worker

There is no hidden completion-port thread pool. The pump thread does its own dequeue, inside
`PumpP2Pmsg` Step 3 (`:4549`):

```cpp
if ( pP2PmsgPump->m_hIOCP )
{
  GetQueuedCompletionStatus ( pP2PmsgPump->m_hIOCP, &dwBytes, &ulCompletionKey, &pOVERLAPPED, dwTimeout );
  …
  pCon -> On_QueuedCompletionStatus ( 0, dwBytes, pOVERLAPPEDcon );
```

With no IOCP, Step 4 parks on the pump's own event instead (`:4601`,
`WaitForSingleObject(m_hQueEvent, dwTimeout)`). Each pump owns its IOCP handle, its
`OVERLAPPED`, its FIFO and its `m_oCSection` — thread affinity is what makes that per-pump
state safe to touch without locking on the consumer side.

> Not a pump count: the `8` in `CreateP2PmsgHub(oP2Paddr, pHub, nPumpsMax, 8)`
> (`P2PeerHub.cpp:332`) is `nNumberOfConcurrentThreads` for `CreateIoCompletionPort`
> (`:1783`) — a kernel hint about how many threads the port may keep runnable. It creates
> nothing.

---

## 6. So a hub thread holds both a hub and a pump?

Yes — and it is the *same thread ID* serving as both identifiers. On the thread `SpawnHub`
creates:

```cpp
Factory ( nPumpsMax )                        // P2Pwin32.cpp:886
{
  P2PmsgHubMgr *pMgr = new P2PmsgHubMgr ( ); // ctor: m_nHubID = GetCurrentThreadId()
                                             //       s_ThreadID_P2PmsgHub.SetAt(m_nHubID, this)
  …
  pMgr -> CreateP2PmsgPump ( );              // ctor: m_nPumpID = GetCurrentThreadId()
}                                            //       s_ThreadID_P2PmsgPump.SetAt(m_nThreadId, this)
```

Both registrations happen on one thread, so `m_nHubID == m_nPumpID` for pump #0.
`CreateP2PmsgHub` then reads the pump straight back out of the map on that same thread and
hangs the completion port on it (`:1777-1785`).

That identity is why `PostP2PeerCon`'s default is not a type confusion
(`P2PeerHub.cpp:485`):

```cpp
if ( nPumpID <= 0 )
    nPumpID = m_nHubID;      // a HubID used as a PumpID — same number, for pump #0
```

and why `RunHub` is simultaneously the hub loop and pump #0's loop: it asserts
`m_nHubID==GetCurrentThreadId()` and then calls `PumpP2Pmsg`, which resolves the pump by
current thread. One `while` loop, both roles.

Two consequences:

- **You cannot create a hub on an existing worker-pump thread.** `CreateP2PmsgHub` throws if
  the calling thread already has a pump (`:1741-1747`, "P2PmsgPump already exists in contect
  of thread" — sic). Hub creation must be what establishes pump #0, so it needs a clean
  thread.
- **Pump #0 consumes a slot.** `CreateP2PmsgPump` does `m_oCListP2PmsgPump.AddTail(...)` and
  the limit check is `GetCount() >= m_nPumpsMax` (`:2400`). With the default 15, a hub has 14
  spare slots, not 15 — and `CreateP2Pexplorer` would take one of those too.

### Thread roles

| Role                        | In hub map | In pump map | Created by                |
| --------------------------- | ---------- | ----------- | ------------------------- |
| Hub thread                  | yes        | yes (pump #0) | `SpawnHub` / `CreateHub` |
| Worker pump thread          | no         | yes         | `SpawnPump`               |
| Outside thread (your `main`) | no        | no          | —                         |

If you use `CreateHub()` to host a hub on a thread you already own (the MFC / service-thread
pattern), that thread *becomes* a pump thread: it cannot host a second pump afterwards, and
you are responsible for calling `PumpP2Pmsg` on it.

---

## 7. What happens if a pump thread throws an exception?

Four nested containment layers. Which one catches it decides whether you lose a connection,
the pump, or the process.

### Layer 1 — the handler dispatch

`DispatchP2Pmsg` wraps all `On_*` map routing in `try` (`P2Pwin32.cpp:3009`) with one live
handler (`:3279`):

```cpp
catch ( P2Pevent *pEVT )
{
  pEVT->Print();
  if ( pP2Pmsg->nCode == CN_P2PeerCon || pP2Pmsg->pCon )
    pP2Pmsg -> pCon -> Drop ( pEVT->Isolate() );
  else
    pEVT -> Cancel ( );
}
```

**Connection dies, pump survives.** `Isolate()` detaches the event from the thread chain so it
can ride along on the con; the drop then passes through your `ON_P2PeerCon_CLOSE` handler.

The catch is type-specific — **`P2Pevent*` only**. A `std::bad_alloc`, an MFC `CException*`,
or anything you throw yourself goes straight past it.

### Layer 2 — timer and IOCP callbacks

Steps 1 and 3 of `PumpP2Pmsg` guard their callbacks individually, and these are sealed —
`catch(P2Pevent*)` **and** `catch(...)`, both ending in `Drop` (`:4432` for `On_PITimer`,
`:4573` for `On_QueuedCompletionStatus`):

```cpp
try { pP2Pmsg->pPeerio->On_PITimer ( … ); }
catch ( P2Pevent *pEVT ) { pP2Pmsg->pPeerio->GetP2PeerCon()->Drop(pEVT->Isolate()); }
catch ( ... )            { … pCon -> Drop ( pEVT->Isolate() ); }
```

Note the asymmetry: Steps 2/2b (normal message dispatch) have no wrapper at this level and
rely entirely on layer 1, which only handles `P2Pevent*`.

### Layer 3 — the pump loop

`RunPump` (`P2PeerTarget.cpp:257-292`) and `RunHub` (`P2PeerHub.cpp:375-436`) each wrap their
whole `while (PumpP2Pmsg(...))` loop in `catch(P2Pevent*)` + `catch(...)`, both ending in
`Cancel()`.

This is where a non-`P2Pevent` throw from your handler lands — and the recovery is to **fall
out of the loop**. The pump stops permanently. Nothing restarts it, nothing signals the peer,
and the hub's other pumps keep running as if nothing happened.

### Layer 4 — the thread trampoline

`ProcPump` (`P2PeerTarget.cpp:206`) and `ProcHub` (`P2PeerHub.cpp:330`) guard the
setup/teardown that sits *outside* layer 3. `ProcHub`'s comment says why:

> An exception escaping a thread proc terminates the process (`std::terminate` on Linux,
> likewise on Windows). `RunHub()` self-guards its own pump loop, but the `CreateP2PmsgHub()`
> setup and `CloseP2PmsgHub()` teardown-drain run outside it and can throw a P2Pevent under
> socket/port pressure — which aborted the process on a pump thread (Phase-5 teardown
> stress).

Past layer 4 there is nothing; `catch_ALL_Cancel` is the backstop.

### What state is left behind

**Normal exception path (layers 1-3):** clean. `RunPump` returns, `ProcPump` zeroes
`*pnPumpID` and calls `CloseP2PmsgPump()`, deregistering from `s_ThreadID_P2PmsgPump` — so
`ClosePump`'s spin on `P2PmsgPumpExists` terminates instead of hanging. The hub side is
equivalent, and `RunHub` does `StoreHubID(m_nHubID, 0)` as its "Manadatory last operation"
(`:441`) on both paths, which is what `SpawnHub`/`CloseHub` spin-read.

**Throw from `CreateP2PmsgPump` itself (layer 4):** `CloseP2PmsgPump()` is inside the same
`try`, after the throwing call, so it is skipped and the thread dies. Not a leak in practice,
because `SpawnPump` polls the thread while waiting for registration
(`P2PeerTarget.cpp:162`):

```cpp
if ( !GetExitCodeThread(hThread,&dwExitCode) || dwExitCode != STILL_ACTIVE )
  return 0;                      // Operational failure
```

Same mechanism on `SpawnHub` — that is the path that produces
`"FATAL: server SpawnHub failed."`.

### What you observe in DmxMeshTest

Nothing crashes and nothing prints on your console. `Cancel()` reports through the event/sink
system, not through the harness's `wprintf` logging. The hub thread silently stops pumping,
the login handshake never completes, `WaitForSingleObject(g_hDoneEvent, …)` times out, and
you exit **3 (TIMEOUT)** — indistinguishable from a genuinely slow handshake.

`AssertReportHook` (`DmxMeshTest.cpp:179`) covers CRT asserts (exit 2); it does not see
exceptions. When debugging an unexplained exit 3, add an override of `On_P2Pevent` to log
what the sink is swallowing, and a log line in `ON_P2PeerCon_CLOSE` to catch the layer-1
`Drop` path. The one place the framework prints unconditionally is `pEVT->Print()` at
`:3282`, so a dropped connection leaves a trace but a layer-3 pump death does not.

> **In the Light and COM trees:** all four layers are still there and still behave exactly as
> described — they are inside `TargetCore`, and the facade does not intercept them. What
> changes is the fifth layer the facade adds at its own boundary: every exported method is
> wrapped, so **no exception ever crosses the ABI**. A `Msgexception` from the kernel becomes
> an `HRESULT` return; the `p2pf::IP2PHubEvents` sink is invoked inside that wrapper, so a
> throw from *your* handler is caught before it can reach the pump loop and kill it
> (layer 3).
>
> That closes the "pump dies quietly" hole from the client's side but not the kernel's: a
> layer-3 death originating *inside* TargetCore still stalls the hub silently, and the
> symptom is still an exit 3 that looks like a slow handshake. The facade gives you one more
> place to look — `onError`, which surfaces the kernel's diagnostics as text. Both Light and
> COM harnesses register it on every hub for exactly that reason, and it is how
> [Q16](#16-why-must-the-side-that-dialled-speak-first) was diagnosed.

---

## 8. Hub map vs pump map — and is the sender the same thread as the receiver?

### The two maps

Independent global registries, both keyed by thread ID, each with its own lock:

|            | `s_ThreadID_P2PmsgHub`                            | `s_ThreadID_P2PmsgPump`                                        |
| ---------- | ------------------------------------------------- | -------------------------------------------------------------- |
| Value      | `P2PmsgHubMgr*`                                   | `P2PmsgPump*`                                                   |
| Lock       | `s_oCSectionP2PmsgHub`                            | `s_oCSectionP2PmsgPump`                                         |
| Holds      | `m_oP2Paddr`, connection list, sink map, pump list, `m_nPumpsMax` | FIFO + `m_oCSection`, timer list, `m_hIOCP`, `m_hQueEvent`, `m_pTarget` |
| Answers    | *which node is this*                              | *which runnable queue is this*                                  |

They are **not thread-local storage** — they are global maps, and the ID you pass need not be
your own. That gives each map two uses:

```cpp
Lookup( GetCurrentThreadId(), … )   // "what context am I in?"     — PumpP2Pmsg:4381
Lookup( nPumpID,             … )   // "reach into someone else's" — PostP2Pmsg:2538
```

Being in the **hub map** makes a thread the owner of a mesh identity. Being in the **pump
map** makes a thread *addressable as a delivery destination* by any other thread.

### Sender and receiver are different threads

That is the design, not an accident. Three hops, three answers.

**1. Posting to a pump — producer thread != consumer thread.** `PostP2Pmsg` runs on the
*caller's* thread but resolves the *destination* pump by ID and appends under that pump's
lock (`:2538, 2547`), then wakes it (`:449`):

```cpp
Wakeup ( DWORD nP2PumpID )
{
  if ( nP2PumpID == GetCurrentThreadId() ) return TRUE;   // self-post: already running
  if ( m_hIOCP )          PostQueuedCompletionStatus( m_hIOCP, 0, 0, m_pOVERLAPPED );
  else if ( m_hQueEvent ) return SetEvent ( m_hQueEvent );
}
```

The explicit same-thread elision tells you cross-thread is the expected case.

**2. Across the Dmx connection — definitively two threads.** `oClient.PostP2PeerMsg(...)`
runs on the client hub thread. `P2PeerioDmx::SendP2PeerMsg` (`P2PeerioDmx.cpp:74`) takes the
global `g_oCSectP2PeerConDmx`, parks the buffer, then reaches into the **peer** object:

```cpp
P2PeerioDmx *pThat = (P2PeerioDmx *)hFile;
…
pThat -> m_pOVERLAPPEDrecv = 0;
pThat -> m_pCon -> PostOVERLAPPED ( pOVERLAPPEDrecv );
```

`PostOVERLAPPED` (`P2PeerCon.cpp:838`) is a `PostQueuedCompletionStatus(m_hCPort, …)` onto the
**server's** completion port.

The comment above the send is literal about what this buys: *"Receiver does the copy. The
buffer effectively sits idle locally until the receiver becomes available."* Nothing is copied
on the sending thread — Dmx hands over a pointer plus a completion, and the receiving thread
does the work. That is the whole "direct memory exchange" idea.

**3. After arrival, within the endpoint — same thread.** Once the frame lands, the recv
completion and your `On_P2PeerMsg` handler run on the same pump thread. That is what the W3
optimisation exploits (`P2Pwin32.cpp:2589`):

> The recv completion (`On_QueuedCompletionStatus`) already runs on the destination pump
> thread, so re-posting the parsed frame to that pump's FIFO only to dequeue and dispatch it a
> loop-iteration later is a redundant same-thread hop.

```
client hub thread                        server hub thread
─────────────────                        ─────────────────
PostP2PeerMsg
  SendP2PeerMsg  ──[g_oCSectP2PeerConDmx]──►  (buffer handed over)
  PostOVERLAPPED ──[PostQueuedCompletionStatus → server m_hCPort]──►
                                           PumpP2Pmsg Step 3
                                             On_QueuedCompletionStatus
                                               → recv + copy
                                               → On_P2PeerMsg   ← your handler
```

Related detail: Step 3 stamps `pCon->m_hCPortP2PumpID = pP2PmsgPump->m_nThreadId` (`:4565`).
The connection remembers which pump thread serviced it, so subsequent traffic keeps landing on
the same thread — connection-to-pump affinity, established on first completion.

---

## 9. Where does the login handshake actually run?

Entirely on the two hub pump threads. The handshake has no thread of its own — it is a state
machine that each pump advances one leg at a time, alternating across the Dmx pair.

**main thread** does setup only — `ServiceFactory`/`ClientFactory`, then `PostP2PeerCon` hands
each con to a hub's pump #0. Then it blocks on `g_hDoneEvent`.

**Client hub thread** — connect completes, the pump dispatches `CN_P2PeerCon`/connect →
`On_ConConnect` → `P2PeerTarget::On_ConConnect` (`P2PeerTarget.cpp:2348`):

```cpp
pCon -> OnConnect ( );
if ( pCon->GetP2Peerio()->IsEncrypted() )  pCon -> PKeyXChange ( nullptr, 0 );
else                                       pCon -> Login ( strP2PaddrNULL, 0, 0 );
```

Dmx is not encrypted, so it goes straight to `Login`, which builds a `P2Pmsg_Login` and posts
it — `SendP2PeerMsg` → `PostOVERLAPPED` onto the **server's** completion port.

**Server hub thread** — Step 3 dequeues → `P2PeerCon::On_QueuedCompletionStatus` →
`RecvP2PeerMsg` parses the frame → the special interception at `P2PeerCon.cpp:370`:

```cpp
if ( pMsg->Map_MatchName(P2Pmsg_Login) && !pMsg->GetSource() )
  PostP2Pmsg ( m_oThatP2Paddr, CN_P2PeerCon, P2P_Login, this, pMsg, m_hCPortP2PumpID );
```

Note the target `m_hCPortP2PumpID` — its *own* pump, the one that just took the completion.
The next pump visit dispatches through `ON_P2PeerCon_LOGIN` → `P2PSig_ConLogin` →
`On_ConLogin` → base (`P2PeerTarget.cpp:2510`):

```cpp
pCon -> OnLogin    ( strThatP2Paddr );
pCon ->   LoginAck ( strThatP2Paddr, 0, 0 );
```

`LoginAck` (`P2PeerCon.cpp:1920`) posts `P2Pmsg_LoginAck` back across Dmx onto the **client's**
port, then `SetState(ConState_Login, 0)`.

**Client hub thread** — same recv path, `Map_MatchName(P2Pmsg_LoginAck)` → posted to its own
pump (`:375`) → dispatched as `P2PSig_ConLoginAck` → the harness's `On_ConLoginAck`
(`DmxMeshTest.cpp:93`), which calls the base (→ `pCon->OnLoginAck`) and then
`PostTestMessage()`.

So the BCast is sent from inside a handshake callback, on the client hub thread. It crosses to
the server hub thread the same way, lands in `On_P2PeerBCast` → `PrintMessage` →
`SetEvent(g_hDoneEvent)`, which finally releases `main`.

```
main            client hub thread              server hub thread
────            ─────────────────              ─────────────────
PostP2PeerCon ─►
                                          ◄─── Listen/Accept
                On_ConConnect
                  Login ──────────────────────► On_ConLogin
                                                  OnLogin
                On_ConLoginAck ◄──────────────── LoginAck
                  PostTestMessage
                    BCast ────────────────────► On_P2PeerBCast
WaitForSingle ◄───────────────────────────────── SetEvent
```

### The framework enforces the thread context

`OnLogin`, `LoginAck` and `OnLoginAck` all open with a context assertion
(`P2PeerCon.cpp:2024`):

```cpp
if ( !CheckP2PmsgPumpState(CN_P2PeerCon,P2P_LoginAck) )
  EVERR->…->Message("Requires ON_P2PeerCon_LOGINACK handler state")->Throw();
```

`CheckP2PmsgPumpState` (`P2Pwin32.cpp:5085`) does `Lookup(GetCurrentThreadId(), pPump)` then
compares `pPump->m_pP2Pmsg->nCode/nMsg` — checking three things at once: you are on a pump
thread, that pump is mid-dispatch, and the message it is dispatching is exactly this one.
These methods are only callable from inside their own handler; calling `pCon->Login()` from
`main` throws.

### You can verify it directly

`DmxMeshHub::Trace` already prints `tid=` (`DmxMeshTest.cpp:136`). In a successful run you see
`On_ConConnect` and `On_ConLoginAck` on one TID, `On_ConListen`/`On_ConAccept`/`On_ConLogin`
on another, both different from the `MAIN` lines.

It also explains the failure shape: because each leg only advances when the owning pump runs,
a pump that dies (layer-3 exception) stalls the alternation with no error on the console.

> **In the Light and COM trees:** the same alternation runs, unchanged, on the same two pump
> threads. The whole of it collapses into one callback:
>
> ```cpp
> hub.onPeerUp([](const wchar_t* peer) { /* the link is logged in */ });
> ```
>
> `onPeerUp` fires from the facade's `On_ConLoginAck` / `On_ConLogin` handling, so it is the
> client's view of the last leg of the diagram above. The seven `On_Con*` trace overrides the
> originals carried — `On_ConStartup`, `On_ConConnect`, `On_ConAccept`, `On_ConListen`,
> `On_ConLogin`, `On_ConClose`, `On_ConShutdown` — have no facade equivalent at all. **If you
> need to watch the handshake leg by leg, that is a reason to use TargetCore directly**; the
> facade deliberately reports only the two edges that matter to an application, `onPeerUp`
> and `onPeerDown`.
>
> The consequence that *does* leak through is [Q16](#16-why-must-the-side-that-dialled-speak-first):
> `onPeerUp` on the listening side fires when *its* leg completes, which is not the same
> instant as the dialling side's.

---

## 10. How many pumps does a hub really have?

**One, everywhere in this codebase** — including both hubs in `DmxMeshTest`.

The multi-pump structure is real: `P2PmsgHubMgr` holds `CList<P2PmsgPump*>
m_oCListP2PmsgPump` plus `m_nPumpsMax`, the global `CreateP2PmsgPump` enforces
`GetCount() >= m_nPumpsMax` → "Attempt to exceed configured pump limit (%i) for hub", and
`RunHub` enumerates the list on shutdown to signal every pump:

```cpp
P2PumpID nPumpID = 0;
while ( EnumP2PmsgPump(m_nHubID,nPumpID) )
  SignalP2PmsgPump ( nPumpID, P2PsigPump_CLOSE );
```

**But nothing in the tree ever adds an ordinary second pump.** `SpawnPump` has no call sites
at all — grepping `*.cpp`/`*.h`/`*.md` across the whole repo returns only the declaration
(`P2PeerTarget.h:84`), the definition (`P2PeerTarget.cpp:136`), and two doc comments. The
`P2PeerTarget::CreatePump()` those comments point at (`P2PeerTarget.cpp:179`,
`P2PeerExplorer.cpp:257`) does not exist anywhere.

The one live path that gives a hub a second pump is the **explorer pump**: `CreateP2Pexpump`
(`P2Pwin32.cpp:1019`) → `CreateP2Pexplorer()` → `AddTail` onto the same list, spawned by
`P2PeerExplorer::SpawnExpump`. That is on-demand diagnostics, driven by a `CREATE`/`SPAWN`
command arriving as a message (`P2PeerHub.cpp:1084-1099`), not anything a normal hub does. It
is also tracked separately in `m_pP2Pexplorer`.

So `nPumpsMax = 15` is headroom for a facility that exists but is unused, and the
list-plus-enumerate machinery is written for N while N has always been 1. "Concurrency within
a node comes from adding pumps" describes a capability, not a path anything currently takes.
`SpawnPump` looks complete — thread creation, registration spin-wait, `ProcPump` guard,
`ClosePump` — but any use would be the first caller, so treat it as untested.

### What a hub's `P2PmsgHubMgr` actually holds in DmxMeshTest

| Field                  | Value                                        |
| ---------------------- | -------------------------------------------- |
| `m_oP2Paddr`           | `DmxMesh.Server` / `DmxMesh.Client`          |
| connection list        | 1 con each                                   |
| `m_pP2PmsgSinkmap`     | null until a sink is created                 |
| `m_oCListP2PmsgPump`   | exactly 1 entry — pump #0, on the hub thread |
| `m_nPumpsMax`          | 15, of which 1 is used                       |
| `m_pP2Pexplorer`       | null                                         |

### Vestigial detail

`m_apP2PmsgPump` — the slot array allocated in `Factory` (`:887`) — is zeroed and freed but
never written to. The live collection is `m_oCListP2PmsgPump`, and that is what the limit
check counts. `P2PmsgHubMgr::GetP2PmsgPump(int)` (`:893`) indexes the array and therefore
always returns null. Do not confuse it with the static `P2PmsgPump::GetP2PmsgPump()`, which
resolves through the thread map and does work.

---

# Part II — the layers

---

## 11. Which tree should I write against?

Start from what your client is, not from what the framework offers.

| If you… | Use | Because |
| ------- | --- | ------- |
| are writing a script, a VBA macro, a .NET app, or anything you would rather not compile against a C++ SDK | **COM** | no build-time dependency on MSCS at all |
| are writing C++ and want the messaging without the framework | **facade** | one header, one import lib, no MFC, no macros |
| need `PostP2Pmsg` pump injection, the `P2PeerMsg` factories, custom `P2PeerCon` subclasses, the priority queue, or the `On_Con*` handshake legs | **TargetCore** | none of that exists above it — see [Q17](#17-what-can-the-layers-not-do) |
| are working the Linux port | **TargetCore** | the facade is a Windows MFC DLL; COM adds the registry and apartments on top |
| are debugging the kernel itself | **TargetCore** | the layers hide exactly the machinery you need to see |

The layers are additive in cost and subtractive in reach. Nothing above TargetCore can do
anything TargetCore cannot; the question is only how much of it you need.

Concretely, from the three trees' own harnesses (non-comment, non-blank lines, all eleven):

| | TargetCore | facade | COM |
| --- | ---: | ---: | ---: |
| harness code | 2376 | 922 | 827 |
| shared helper | — | 79 | 387 |

The COM harnesses are the shortest and the COM *stack* is the largest. That is the trade
being made: the plumbing did not vanish, it moved into one place that a scripting client
never sees.

---

## 12. What happened to the startup and shutdown boilerplate?

Every original harness carried this, in this order, on *every* exit path — and there are six
or more returns in some of them:

```cpp
StartupP2Pmsg(16);  WSAStartup(MAKEWORD(2,2), &wsa);
… hub.SpawnHub(); …
hub.CloseHub();  WaitForSingleObject(hThread, 3000);  CloseHandle(hThread);
CleanupP2Pmsg();  CloseHandle(g_hDoneEvent);  WSACleanup();
```

It becomes two objects with destructors:

```cpp
p2pf::Network net;                            // StartupP2Pmsg + WSAStartup, refcounted
p2pf::Hub hub = net.createHub(L"Demo.Server"); // P2PeerHub + SpawnHub
// ~Hub closes the pump and joins; ~Network shuts the kernel down. In order.
```

`p2pf::Network` is a **process-wide refcounted singleton** inside the DLL: every successful
`P2PF_CreateNetwork` hands back the same network and adds a reference, and the kernel comes
up on the first call and goes down on the last. So two independent components in one process
can each hold a `Network` without either one tearing the kernel out from under the other —
which the raw `StartupP2Pmsg`/`CleanupP2Pmsg` pair cannot do (Q2's re-entry guard throws
"P2Pmsg environment already started").

The COM tree adds one thing to remember and removes another. Added: **the coclass must be
registered**, or `CoCreateInstance` returns `REGDB_E_CLASSNOTREG` and nothing else works;
`run_all.ps1` does it with `regsvr32 /n /i:user`, which writes `HKCU\Software\Classes` only
and needs no elevation. Removed: hub lifetime, because the network holds a reference to every
hub it created and releases them all when it goes — the facade's rule, carried through COM.

---

## 13. Where did `BEGIN_P2PeerMsg_MAP` go?

`PipeMsgMapTest` is the harness to read across all three trees, because it exists to
demonstrate exactly this. The original:

```cpp
class PipeMapHub : public P2PeerHub
{
    DECLARE_P2PeerMsg_MAP()
    msgRESULT On_HubPing(P2PeerMsg* pMsg) { … }
    msgRESULT On_HubPong(P2PeerMsg* pMsg) { … }
};
BEGIN_P2PeerMsg_MAP(PipeMapHub, P2PeerHub)
    ON_P2PeerMsg(kMsgPing, On_HubPing)
    ON_P2PeerMsg(kMsgPong, On_HubPong)
END_P2PeerMsg_MAP()
```

The facade:

```cpp
server.onTopic(L"HubPing", [](const p2pf::Message& m) { … });
client.onTopic(L"HubPong", [](const p2pf::Message& m) { … });
```

**A facade "topic" is the kernel message name.** It is not a parallel dispatch system built on
top — the facade keeps one wildcard `BEGIN_P2PeerMsg_MAP` inside `FacadeHub.cpp` and routes
through the kernel's own map machinery, which is why the semantics are identical rather than
merely similar. Two rules survive intact and are visible in the public header:

* names beginning with `P2Pmsg` are the kernel's, and `Send`/`Broadcast` refuse them with
  `P2PF_E_RESERVED_TOPIC`;
* a message matching no registered topic falls through to `onMessage`, the way an unmatched
  name falls through the map to `On_P2PeerUCast`.

In COM the map stops being a language feature at all. There is **one** event —
`_IP2PHubEvents.OnMessage(source, topic, payload, broadcast)` — and "routing by name" is an
ordinary switch on the topic string in the client. That is precisely what makes it
expressible from VBScript or VBA, neither of which has anything a macro-based map could bind
to. `common/ComHarness.h` does the switch so the COM harnesses still *read* like a map.

Related: the `P2PeerMsg` **factories** (`RedirectFactory`, `ResponseFactory`) have no
equivalent above the kernel, and neither does the ownership hazard they came with — see
[Q17](#17-what-can-the-layers-not-do).

---

## 14. Which thread runs my callback?

Different in all three trees, and it is the single most important difference between them.

**TargetCore — the pump thread that owns the connection.** Q8 and Q9 in full: your `On_*`
handler runs on the hub pump thread, the same one that took the IOCP completion, with
connection-to-pump affinity established on first completion.

**Facade — still the pump thread.** The facade does not move your callback; it wraps it. From
the public header:

> Callbacks fire on the hub pump thread; payload pointers are valid only during the callback.

So a slow `onTopic` handler stalls that hub's pump exactly as a slow `On_P2PeerMsg` would, and
the payload rule is the kernel's rule unchanged. What the facade does add is the guarantee
that a *throw* from your handler cannot kill the pump (Q7).

**COM — neither.** A kernel pump thread is not a COM apartment, so calling a client's sink
from it would be illegal for every STA client. Each COM hub therefore:

1. copies every callback — strings **and payload bytes** — into a bounded queue (4096
   entries), on the pump thread, doing nothing else there;
2. replays them on its own dedicated MTA dispatch thread;
3. re-fetches your sink from the **Global Interface Table** per fire, so COM marshals the
   call into whatever apartment the sink actually lives in.

Three consequences worth knowing:

* **A slow client handler can no longer stall the kernel pump.** It stalls the dispatch
  thread instead, and the queue absorbs the difference. Overflow past 4096 is dropped and
  reported as an `OnError` once the queue drains — never silently.
* **The payload-lifetime rule disappears.** The queued copy owns its bytes, so the
  `SAFEARRAY` handed to your sink is valid for as long as you hold it.
* **An STA client must pump messages.** A marshalled call arrives through the message queue,
  so a plain `WaitForSingleObject` in an STA hangs forever with the event sitting undelivered.
  Every COM harness runs STA on purpose and `com::Gate::wait()` pumps; you can see it in the
  logs, where each delivery line carries the same `tid=` as `main`.

| | TargetCore | facade | COM (STA client) |
| --- | --- | --- | --- |
| handler runs on | hub pump thread | hub pump thread | **your** thread, in the pump |
| threads per hub | 1 | 1 | 2 (pump + dispatch) |
| slow handler | stalls the pump | stalls the pump | stalls only the dispatch thread |
| payload valid | during the callback | during the callback | for as long as you hold it |
| must pump messages | no | no | **yes** |

---

## 15. Does arming order still matter?

Less than it did, and the difference is per transport rather than per tree.

Every original harness has a `Sleep(750)` between arming the listener and dialling it, with a
comment explaining that the server pump must reach `Listen()`/`Accept()` first (Q4.4 covers
why, for Dmx). The reason it was *needed* is that a raw `ClientFactory` connection dials
**exactly once**: if the far side is not there yet, that is the end of it.

The facade changes that for the two transports where "not there yet" is a timing question
rather than a configuration error. Its dials set the kernel's `m_uAutoRestart` — 500 ms,
`FacadeHub.cpp:72` — and override `HasDroppedOut` to return `true`, because
`P2PeerCon::OnClose` otherwise shows a **modal message box** per failed retry, which is fatal
for a library.

| Transport | Facade dial retries? | So arming order… |
| --------- | -------------------- | ---------------- |
| TCP (`connect`) | yes, ~2/s | does not matter |
| named pipe (`connectPipe`) | yes, ~2/s | does not matter |
| Dmx (`connectDmx`) | **no** | **listener first** |
| serial (`connectSerial`) | **no** | **listener first** |

The split is deliberate and documented on `IP2PHub`: a missing in-process Dmx service or a
missing COM port is a configuration fault, not a race, so retrying would only hide the
mistake. The Light and COM `DmxMeshTest` and `Com232MeshTest` arm the listener first and say
so in a comment; `WsaMeshTest` and `PipeMeshTest` do not need to, and the Light `WsaMeshTest`
completes its whole round trip in about **17 ms** where the original spent 750 ms waiting.

One thing that follows from retrying dials: `S_OK` from `Connect*` means "the dial is armed",
not "the peer is up". Wait for `onPeerUp`, or poll `IsPeerUp`. A peer that never appears
simply never fires `onPeerUp` and raises no error, because "not up yet" and "not up ever" are
indistinguishable.

---

## 16. Why must the side that dialled speak first?

Because `onPeerUp` on the **listening** side can fire before the dialling side has finished
logging in. Send from it and you race the handshake; the far end answers an early message
with

```
[ERROR] P2PeerCon::On_QueuedCompletionStatus
Application message received before login
ADVICE  : Connection dropped out
```

and drops the whole connection. The harness then times out with no other symptom — an exit 3
that looks exactly like a slow handshake.

This is the one ordering hazard the layers **introduce** rather than remove, and it is a
direct consequence of Q9: the handshake is a state machine alternating across two pump
threads, and each side's `onPeerUp` fires when *its own* leg completes. Those are different
instants. The original harnesses never hit it because they sent from `On_ConLoginAck`, which
by construction only exists on the dialling side.

**The rule:** the first message on a link comes from the side that **dialled**; the listening
side answers on receipt rather than announcing itself. Every harness in both new trees follows
that shape — `LocalInMemoryTest` (Light) is where it was found, and its header records the
diagnosis.

If you genuinely need the listener to speak first, wait for traffic from the far side, or
poll `isPeerUp` from a thread that is not a callback.

---

## 17. What can the layers *not* do?

Four things, each of which cost a harness something real when the trees were rebuilt.

**Pump injection.** `PostP2Pmsg(pMsg, targetHub.GetHubID())` drops a message straight onto
another hub's pump queue with no connection and no handshake. The facade's model is that hubs
talk over connections, so there is no `GetHubID()`, no raw queue, and no way to reach one.
`LocalInMemoryTest` is the harness built on that call: the Light and COM versions reach the
same end state over the Dmx transport and pay one login handshake for it. Note that the
original's threading hazard goes too — "only legal from a non-hub thread, or you trip a
cross-hub-context ASSERT" cannot be expressed above the facade.

**The message factories.** `RedirectFactory` / `ResponseFactory` and the whole question of
which one is safe to standalone-post (the latter inherits the request's routing prefix and
loops the reply back into the sender's own map) have no equivalent. There is one send verb,
it takes a destination and a topic, and there is no envelope to inherit. `PipeMsgFactoryTest`
keeps what was being demonstrated — a caller-built payload routed to a named handler — and
drops the API navigation. The ownership hazard goes with it: the factory returned a heap
message the caller owned and `PostP2PeerMsg` then took, whereas `send()` copies the bytes
before it returns.

**The handshake legs and the connection objects.** No `P2PeerCon`, no `On_ConStartup` /
`On_ConAccept` / `On_ConListen`, no custom connection subclass, no pump selection
(`PostP2PeerCon`'s `nPumpID`), no priority queue. `onPeerUp` and `onPeerDown` are the whole
connection API.

**Linux.** `AlexInterop` is the Linux port's Phase-3 exit criterion and is written to avoid
every Win32-ism so the same source builds against the io_uring shim with g++. The facade is a
Windows MFC DLL, so the Light rewrite forfeits the one property that harness exists for; the
COM rewrite forfeits it twice over, since COM, the registry, apartments and `BSTR` have no
Linux counterpart at all. Both versions exist for completeness of the mapping. **For the
Linux port, use the original.**

One non-limitation worth recording, because it was an open question: **the kernel really does
route a tree.** `RouteLoopbackTest` (Light and COM) builds Root / A / B / Leaf over Dmx and
confirms at runtime that `Leaf → B` is delivered across two hops with no connection between
them, that an address nobody holds is delivered nowhere (with a `Bad destination address`
diagnostic), and that a broadcast from the root reaches every descendant including the
grandchild. Hierarchical dotted addresses are what make that work, in every tree.

---

## 18. What happens to errors, exceptions and asserts?

The originals all carry this, and the comment above it is the reason:

```cpp
_CrtSetReportHook(AssertReportHook);   // a debug ASSERT would pop a MODAL DIALOG
                                       // and hang a headless run
```

That is why the shared exit-code contract has a **2 = an MFC/CRT assertion fired**. In the
Light and COM trees, `2` is unreachable by construction: there is no MFC in those processes,
and the facade reports failures as `HRESULT`s and `onError` callbacks. Neither new tree
installs an assert hook, and neither needs one. (The modal-box problem does not disappear
from the *kernel* — the facade's `HasDroppedOut` override in Q15 exists precisely because
`P2PeerCon::OnClose` shows one per failed retry.)

The error surface, layer by layer:

| | TargetCore | facade | COM |
| --- | --- | --- | --- |
| call failed | `BOOL`, or a thrown `Msgexception` | `HRESULT` | `HRESULT` → `COMException` / `Err.Number` |
| duplicate peer | `PostP2PeerCon` returns `FALSE`; reason is in the source | `P2PF_E_CON_DUPLICATE` | same value, `0x80040204`, out to scripts |
| reserved name | delivered and ignored | `P2PF_E_RESERVED_TOPIC` | `0x80040205` |
| bad argument | undefined / truncated | typed parameters | `E_INVALIDARG` (ports are range-checked) |
| kernel diagnostics | `pEVT->Print()` to the console | `onError(text)` | `OnError` event |

`TwoConTest` is the harness that shows the whole column: the original had to read
`P2PeerHub.cpp:432-447` to explain a `FALSE`, the Light version gets a named error, and the
COM version demonstrates the same value arriving in a scripting client that can branch on it.

Two COM-only notes. Ports cross as `LONG` rather than `unsigned short`, so the layer
range-checks them — `Listen(peer, 70000)` is `E_INVALIDARG` instead of a silently truncated
port. And `Broadcast` had to change shape: the facade returns `S_FALSE` for "nobody was up",
which no automation client can see, so the COM method returns `Delivered` as an out value
instead.

---

## 19. How do events reach a COM client — and why can't PowerShell sink them?

A COM client gets events through a **connection point**. `IP2PHubCom` exposes
`IConnectionPointContainer`; you `FindConnectionPoint(DIID__IP2PHubEvents)` and `Advise` an
`IDispatch` implementing the four-method dispinterface. `common/ComHarness.h` contains that
sink, hand-written in about 60 lines, and it is the code every early-bound client needs —
whether it writes it (C++), or its host generates it (VB6, C# with an interop assembly).

The delivery path is [Q14](#14-which-thread-runs-my-callback): pump thread → bounded queue →
dispatch thread → GIT re-fetch → your apartment.

**The scripts in `ComExamples/script` do not sink events, and that is a host
limitation rather than a gap in the layer.**

* **PowerShell / .NET** can only bind COM events through an interop assembly for the coclass,
  which needs `TlbImp` or an early-bound reference. Late-bound, a `System.__ComObject` has no
  events to bind — `Get-Member` on a hub shows every method and property and no `Event` rows
  at all. This is not specific to TargetCom.
* **WSH / VBScript** can sink events, but only for objects it created itself via
  `WScript.CreateObject(progid, prefix)`. Hubs come from `Network.CreateHub`, so they are
  outside that mechanism.

What a script *can* observe end to end is delivery itself, and both scripts use it rather than
claiming more than they prove: `IsPeerUp` only goes True after the kernel's login handshake
completes, and `Broadcast` returns True only if at least one peer was up to take a copy.

`script/vbs_client.vbs` is deliberately the least capable client the layer will ever have — no
byte arrays, no structs, no `HRESULT` inspection beyond `Err.Number`. It passes under
`cscript`, which is the strongest single statement about the COM layer: if it works there, it
works from VBA, from an Excel macro and from a logon script.

---

## Quick reference

### The kernel — true underneath all three trees

| Question                                | Answer                                                            |
| --------------------------------------- | ----------------------------------------------------------------- |
| Hubs per process                        | `StartupP2Pmsg(nMaxHubs)`, hard cap `MAX_P2PmsgHub = 256`          |
| Pumps per hub                           | `nPumpsMax` slots, but always 1 in practice                        |
| Threads per hub                         | 1 (the hub thread, which hosts pump #0)                            |
| Threads per pump                        | 1, and `PumpID == thread ID`                                       |
| HubID                                   | `GetCurrentThreadId()` of the creating thread                      |
| Can a thread host 2 hubs or 2 pumps?    | No — the thread ID is the key in both registries                   |
| Can a thread host 1 hub *and* 1 pump?   | Yes — that is exactly what a hub thread is                         |
| Who runs the handshake?                 | The two hub pump threads, alternating                              |
| Who runs your `On_*` handlers?          | The pump thread owning that connection                             |
| Exception in a handler                  | `P2Pevent*` → connection dropped; anything else → pump dies quietly |

### Across the three trees

| | TargetCore | facade | COM |
| --- | --- | --- | --- |
| One hub is | a `P2PeerHub` + 1 thread | the same, hidden | the same, **+1 dispatch thread** |
| Hubs per process | your `StartupP2Pmsg(n)`, up to 256 | **16**, hardcoded | **16**, hardcoded |
| Startup / shutdown | 8 calls, on every exit path | `p2pf::Network` ctor/dtor | `CoCreateInstance` + release |
| Routing by name | `BEGIN_P2PeerMsg_MAP` macros | `onTopic(name, λ)` | one `OnMessage` event + your switch |
| Your handler runs on | the hub pump thread | the hub pump thread | your apartment (you must pump) |
| Payload valid | during the callback | during the callback | for as long as you hold it |
| Slow handler | stalls the pump | stalls the pump | stalls only the dispatch thread |
| Dial ordering | always matters (one attempt) | TCP/pipe retry; Dmx/serial do not | same as facade |
| First message on a link | from `On_ConLoginAck` | **from the side that dialled** | **from the side that dialled** |
| A failed call | `FALSE` or a throw | `HRESULT` | `HRESULT` → `COMException` |
| A debug ASSERT | modal dialog → needs a hook | cannot happen (no MFC) | cannot happen |
| Needs registering | no | no | **yes** (`regsvr32 /n /i:user`) |
| Works from a script | no | no | **yes** (methods; not events) |
| Runs on Linux | the kernel does, via the io_uring shim — a harness needs a portable sibling (`AlexInterop`, `dmx_mesh.cpp`, `wsa_mesh.cpp`) | no | no |

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](LICENSE) for
the full text.

# dotNetExamples

Every harness in [`DirectExamples`](../DirectExamples), rewritten a fourth
time — in **C#**, on the same [TargetCom](../../TargetFacade/com) ATL layer that
[`ComExamples`](../ComExamples) drives from C++.

Same questions, same verdicts, same exit-code contract as the other three trees,
so all four read side by side.

```
.\build.ps1                          # csc, x64, .NET Framework 4.8
.\run_all.ps1                        # register per-user, run everything, unregister
.\run_all.ps1 -Config Release
```

**Status: 13/13 on Debug|x64 and 13/13 on Release|x64** (11 harnesses; the two
two-process ones count their server and client separately), measured 2026-08-13
against a freshly rebuilt kernel.

`RouteLoopbackTestNet` briefly regressed against that kernel and is green again —
see [downward relay](#downward-relay-and-security_review-m2) below.

The Release **access violation during process teardown** in
`AlexInteropNet (client)` is still there and still intermittent — raised *after*
the harness printed `Done (exit=0)`, so the test itself completed and its verdict
stands. Measured at 4 runs in 6, so a given Release run may show 12/13 or 11/13;
the run recorded above happened not to trip it.

It is **not caused by the ABI-4 migration below**, and it is not a C# problem
either. `FacadeExamples\AlexInteropLight` — the flat-C++-ABI harness
asking the same question — faults the same way at 2-3 runs in 6, and that one
was checked against a pre-migration baseline (unmodified sources against the
pre-ABI-4 facade: 3 in 6). The shared factor is the facade/kernel Release
teardown, not the binding. Curiously the C++ **COM** twin, `AlexInteropCom`,
does not fault at all in 6 runs; its teardown goes through `CoUninitialize` and
the GIT rather than straight out of `main`. Not diagnosed.

## Downward relay and SECURITY_REVIEW M2

For a window, `RouteLoopbackTestNet` scenario **[2]** — `Root → Leaf`, straight
down the tree through A — did not arrive, and the receiving end said why:

```
[ERROR] []P2PeerCon::GateAppMsgInbound(this=)
Message source [RouteMesh.Root] is not the logged-in identity [RouteMesh.Root.A]
ADVICE  : Connection dropped out
```

`P2PeerCon::GateAppMsgInbound` (`P2PeerCon.cpp:2015`) is the SECURITY_REVIEW
**M2 source binding**: the source address is read off the wire, so without a
check a peer that logged in as one identity could attribute its messages to
another. The rule was that a message's source must be the peer's own address **or
a descendant of it** (`IsRable()` — exact match or a hop-boundary prefix).

That is right for a link to a descendant and cannot hold on a link to an
**ancestor**, which is this hub's gateway to the rest of the tree — everything
routed down arrives from it still carrying its original source. The effect was
asymmetric, so only one of the four scenarios failed:

| leg | receiving hub's peer | source | then |
|---|---|---|---|
| **[1]** `Leaf → B`, up then down | B's peer is `Root` | `Root.A.Leaf` | passed — below Root |
| **[2]** `Root → Leaf`, down via A | Leaf's peer is `Root.A` | `Root` | **refused** — above A |

**Fixed** (`P2PeerCon.cpp`, 2026-08-13): an ancestor link is admitted without a
source check; descendant and unrelated links are unchanged.

Nothing managed was ever involved, and the CLR never saw it: the message was
dropped inside the kernel before any callback was raised, so the sink was simply
never called. It was the one failure in this tree that no amount of interop
archaeology would have explained.

## Note on TargetCore's flat C API

The numbers above were taken after `TargetCore_c.{h,cpp,_u8.cpp}` — the flat
`extern "C"` / Panama surface — was removed from `TargetCore.dll` on 2026-08-13.
This tree could not have used it in any case: the harnesses import nothing native
at all, reaching the kernel through `TargetCom` and the registry. Removing it
changed no result here. The sources are preserved in
`MSCS_JavaBindings\TargetCore\native\`.

## Migrated to TargetFacade ABI 4

The facade collapsed its eight typed arming verbs onto **one pair**, and
`IP2PHubCom` was **reissued with a new IID** to match — so
`common\TargetComInterop.cs` had to change on both counts:

```csharp
[Guid("8E203CC9-9100-4812-936C-0AD3263C30AD")]   // was
[Guid("12D65CF2-3F8C-4856-A52F-1B0B35F3B4FE")]   // is

[DispId(3)] void ListenPipe (string peer, string pipeName);          // was, x8
[DispId(1)] void Listen     (string toPeer, string endpoint);        // is, x2
```

The dispids renumbered with them (`Send` 9→3, `SendText` 10→4, `Broadcast`
11→5, `Address` 12→6, `IsPeerUp` 13→7, `Close` 14→8), and every harness changed
one line per arming call — `Com.Endpoint.Pipe(name)` and friends compose the
string. Vtable order still follows declaration order, so the two edits had to
agree; a mistake there lands calls on the wrong member rather than failing to
compile.

**The old IID is the safety net.** A stale copy of this interop against a
current server fails at `QueryInterface` instead of calling `Listen` with an
`int` where a `BSTR` belongs. That is the whole reason the interface got a new
identity rather than a quietly changed shape.

One thing the COM layer lost in the move: its private `Port()` range check went
away with the last numeric parameter, so a bad port is `P2PF_E_ENDPOINT` from
the facade's parser instead of `E_INVALIDARG` from the COM layer. That matters
here more than in the C++ trees, because `E_INVALIDARG` was the one HRESULT
finding 3 below is built on — see `Com232MeshTest`, which now demonstrates it on
`CreateHub("")`, the layer's only surviving argument validation.

`run_all.ps1` registers with `regsvr32 /n /i:user`, i.e. `HKCU\Software\Classes`
only: no elevation, nothing machine-wide, always removed again. It registers the
**staged copy** in `bin\<Config>`, so this directory is self-contained and does
not need `ComExamples` to have been run.

## Why bother, given the COM tree already exists

The C++ COM tree's whole argument is that a COM client has no build-time
relationship with MSCS, and that this is what makes "VB, VBA, Office, WSH,
classic ASP and .NET" possible. It proved the scripting half of that claim with a
PowerShell and a VBScript client, and left one honest gap:

> **The scripts do not** [sink events], and that is a *host* limitation rather
> than a gap in the layer: .NET — and therefore PowerShell — can only bind COM
> events through an interop assembly for the coclass, which needs `TlbImp` or an
> early-bound reference.
> — [`ComExamples/README.md`](../ComExamples/README.md)

This tree is that interop assembly, written by hand as ordinary source
([`common\TargetComInterop.cs`](common/TargetComInterop.cs), 102 lines), and the
managed client that uses it. It closes the gap: **C# gets the events**, through
`_IP2PHubEvents`, with no TlbImp, no generated code and no build step beyond
`csc`.

It also turned up four things about this COM layer that none of the three C++
trees could have found, because all three are C++. The fourth arrived with
ABI 4, and it is the sharpest of them.

| | links / references | binds to the implementation |
|---|---|---|
| `DirectExamples` | `TargetCore.lib` + `Msgcore.lib`, MFC | at link time |
| `FacadeExamples` | `TargetFacade.lib` | at link time |
| `ComExamples` | `ole32`/`oleaut32`/`uuid` + the type library | registry, at run time |
| `dotNetExamples` | **`mscorlib` / `System` / `System.Core`** | registry, at run time |

The C# compiler is handed no MSCS input of any kind — not a header, not a `.lib`,
not a `.tlb`, not a NuGet package.

## Four findings

### 1. A managed sink is agile, so the apartment does nothing

Every harness in both COM trees declares an STA, and the C++ one's logs show
every callback arriving on the main thread — one `tid=` throughout. That is
TargetCom's Global-Interface-Table machinery working exactly as its header
describes: the sink is parked in the GIT, re-fetched on the DLL's own MTA
dispatch thread, and the re-fetch yields a **proxy** that marshals the call back
into the client's apartment.

Do the same from C# and the *identical* code path produces the opposite result:

```
C++  sink : [deliver] ... tid=13276   [deliver] ... tid=13276   (== main)
C#   sink : [deliver] ... tid=21180   [deliver] ... tid=7892    (!= main, != each other)
```

A CLR callable wrapper is **agile**. Marshalling the sink from the STA to an MTA
thread and comparing pointers shows no proxy is created at all — the same address
comes back — so `GetInterfaceFromGlobal` hands the dispatch thread the raw
pointer and the callback simply runs there.

The practical consequence is that **handlers run concurrently, one thread per
hub**, and any state they touch needs to be written as if it were on a thread
pool. `RouteLoopbackTest` counts deliveries with `Interlocked`, where its C++
twin uses a plain `LONG`; `PipeMsgFactoryTest`'s result flag is `volatile`;
console output is serialised under one lock. Get this wrong and you lose counts
under load, on a code path that looks single-threaded in every other tree.

The message pump stays anyway. It costs nothing, `Gate.Wait` needs a wait loop
regardless, and it is load-bearing again the moment any non-agile object enters
the picture — which is a change a client can make by accident.

### 2. `ClassInterface(None)` is mandatory, and TargetCom's `Advise` fallback is what saves it

With the default `ClassInterfaceType.AutoDispatch`, the CCW's `IDispatch` is an
auto-generated class interface and `DISPID`s 1–4 resolve against *that* — the
events would be silently misrouted rather than fail. `ClassInterfaceType.None`
makes the CCW's dispatch identity be `IP2PHubEvents`, which is correct.

The measured cost of `None`: `QueryInterface(IID_IDispatch)` on that CCW returns
`E_NOINTERFACE`. Only `QueryInterface(DIID__IP2PHubEvents)` succeeds. So a
managed sink is connectable **only** because `CP2PHubCom::Advise` tries the DIID
after `IID_IDispatch` fails ([`ComHub.cpp:586`](../../TargetFacade/com/ComHub.cpp)).
A connection point that asked for `IID_IDispatch` alone — a very common way to
write one — would refuse every C# client, and no C++ test could ever notice.

### 3. The CLR rewrites part of the error contract

The facade's HRESULTs were designed to cross to automation unchanged, and mostly
they do. But the CLR maps a fixed table of well-known HRESULTs onto CLR exception
types before the caller sees them. Measured against this interface:

| returned by the COM layer | what C# actually catches |
|---|---|
| `P2PF_E_CON_DUPLICATE` 0x80040204 | `COMException` — custom code, passes through |
| `P2PF_E_RESERVED_TOPIC` 0x80040205 | `COMException` |
| `DISP_E_TYPEMISMATCH` 0x80020005 | `COMException` |
| `E_INVALIDARG` 0x80070057 | **`ArgumentException`** |
| `E_NOINTERFACE` 0x80004002 | **`InvalidCastException`** |

So a C# client that wraps this API in `catch (COMException)` is correct for the
facade's own error range and **crashes on the COM layer's argument validation** —
which is precisely the validation the IDL added *for automation clients*.
`Com.Hub` recovers the original code with `Marshal.GetHRForException` regardless
of which type the CLR picked; `Com232MeshTest` prints both forms side by side.

This one cost a red test before it was understood, which is the best kind.

### 4. A SUCCESS HRESULT other than `S_OK` cannot reach a .NET caller at all

Finding 3 is about failures being *reshaped*. This one is about a success being
**erased**, and it is worse, because nothing anywhere reports a problem.

ABI 4 added `P2PF_S_UNRELATED_LINK` (`0x0004020C`): *the link is armed, but
these two addresses are neither ancestor nor descendant, so nothing can be
routed through the edge and a broadcast will not relay beyond it.* Severity bit
0 — a success code, in the same tradition as `S_FALSE`.

A `void` interop signature **discards** it. There is no exception to catch,
because the call succeeded, and there is no return value, because the marshaller
consumed the HRESULT. So:

```
C++  (both trees) : Listen ( 'TwoConTest.PeerA', 'tcp://:7788' ) -> P2PF_S_UNRELATED_LINK
C#                : Listen ( 'TwoConTest.PeerA', 'tcp://:7788' ) -> S_OK
```

Same call, same server, same instant. Every `X.Server`/`X.Client` pair in these
harnesses is a sibling pair, so every one of them hits this.

The declarations stay `void` anyway, and that is the point rather than an
oversight: **this file's premise is that it is what `TlbImp` and "Add Reference"
would have generated**, and they generate `void`. Making the arming pair
`[PreserveSig] int` here would recover the code and make the tree stop being
representative of what a real .NET consumer gets. The honest finding is the
uncomfortable one: *that information does not reach a normal .NET client.*

It is not lost, though, and the design is better than it first looks. The facade
raises **one `OnError`** alongside the code, carrying the whole sentence — and an
event is exactly what a .NET client is best at receiving. Look for it in any log
here:

```
[HUB] error : TargetFacade: 'TwoConTest.Hub' and peer 'TwoConTest.PeerA' are
              neither ancestor nor descendant. The link is armed and carries
              direct traffic, but nothing can be ROUTED through it ...
```

The lesson generalises past this interface: **if an automation-facing method has
something to say on success, it must say it in an `[out, retval]` or an event,
never in the HRESULT.** `Broadcast` already got this right — the IDL reshaped it
to return `Delivered` as a value precisely because `S_FALSE` is invisible to
automation. `P2PF_S_UNRELATED_LINK` is the same problem, and for a while only
the event saved it.

#### How it was actually fixed: ask the hub afterwards

`IP2PHubCom` grew a **read side** — dispids `9`–`13`, appended after `Close`, no
new IID and no call site moved: `RelationTo`, `ConCount`, `PeerAt`,
`EndpointFor` and `Description`. They are declared at the end of
`common\TargetComInterop.cs` and wrapped in `Com.Hub`.

The arming pair stays `void`, because that is still what `TlbImp` generates and
this tree still has to look like what a real consumer gets. What changed is that
the fact is no longer *only* on an event: it is a value, readable at any time,
by any tier — including the script hosts that could never sink `_IP2PHubEvents`
at all.

`TwoConTest` prints both, one after the other, which is the clearest statement
of the whole finding available anywhere in these trees:

```
[HUB] Listen ( 'TwoConTest.PeerA', 'tcp://:7788' )  -> S_OK
[HUB] RelationTo( 'TwoConTest.PeerA' ) -> 0x0801  (unrelated: direct traffic only, no transit)

address=TwoConTest.Hub
con=TwoConTest.PeerA	tcp://:7788	listen,unrelated
con=TwoConTest.PeerB	tcp://127.0.0.1:7788	dial,unrelated
```

`0x0801` is `ConListen | RelUnrelated`. The `S_OK` above it is still the
marshaller talking; the line below it is the hub.

One caveat that comes with appending members under an **unchanged IID**: the new
five have no protection against a stale registered server, because the IID that
guards the rest of this file did not change. A call to dispid `9` on a
pre-read-side build runs off the end of that build's vtable. Unregister an old
`TargetCom` before running against a current one.

## Layout

```
common\TargetComInterop.cs   the type library by hand: [ComImport] IP2PHubCom,
                             IP2PNetworkCom, _IP2PHubEvents, the coclass, the
                             HRESULTs
common\ComHarness.cs         Apartment / pump / Gate / Message / EventSink /
                             Hub / Network -- the C# twin of ComHarness.h
<Harness>\<Harness>.cs       one file per harness
build.ps1                    csc -> bin\<Config>, plus DLL staging
run_all.ps1                  register + build + run + summarise + unregister
bin\<Config>\                all exes plus the four staged DLLs
logs\<Config>\               one log per harness
```

## Toolchain

**.NET Framework 4.8, x64, built by Roslyn `csc` directly.** No `.csproj`, no
SDK, no NuGet, no targeting pack — every harness is `common\*.cs` plus one file,
referencing three framework assemblies. `build.ps1` finds `csc` in the Visual
Studio install and falls back to the in-box `Framework64` compiler, so the tree
builds on a box carrying only the C++ workload, which is what MSCS machines
usually have. If you would rather have VS integration, an SDK-style `.csproj`
with `<TargetFramework>net48</TargetFramework>` over these same sources builds
identically.

x64 is not optional: TargetCom and the kernel under it are 64-bit, and AnyCPU
would load a 32-bit CLR on some hosts and fail `CoCreateInstance` with a
class-not-registered that has nothing to do with registration.

## How much code

Non-comment, non-blank lines, counted the same way for all four trees:

| | original | Light | COM | **C#** |
|---|---:|---:|---:|---:|
| eleven harnesses | 2376 | 930 | 841 | **904** |
| shared helper | — | 106 | 408 | **402** |

The C# harnesses come out slightly *longer* than the C++ COM ones, and it is
worth being precise about why rather than rounding it away. Eight of the eleven
are within ±6 lines of their twin. The whole difference is three files that do
more than their twin did: `PipeMsgFactoryTest` (+31, it also round-trips a
string payload and shows the `DISP_E_TYPEMISMATCH` in its native `catch` form),
`TwoConTest` (+21, PART B is now the exception the C++ README predicted), and
`Com232MeshTest` (+13, finding 3 above).

The shared helper is a wash — 402 against 408 — but its composition is not. Of
the C++ helper's 408 lines, ~120 are a hand-written `IDispatch`: `QueryInterface`,
`AddRef`, `Release`, `Invoke`, `DISPPARAMS` decoding in reverse argument order,
and `SAFEARRAY` unpacking. In C# that entire block is:

```csharp
[ComVisible(true), ClassInterface(ClassInterfaceType.None)]
public sealed class EventSink : IP2PHubEvents
{
    public void OnMessage (string source, string topic, object payload, bool broadcast) { ... }
    public void OnPeerUp  (string peer) { ... }
    ...
}
```

What the C# helper spends its lines on instead is the interop *declarations* —
which is the honest trade, because that is the part TlbImp would have generated.

Both helpers got a few lines shorter at ABI 4 and then spent them again: eight
transport wrappers became two, and five endpoint composers (`Com.Endpoint`)
took their place. Character for character the same five in all three facade
trees, because the endpoint string is never translated on the way down — C#
hands a `string` to the marshaller, the marshaller makes a `BSTR`, TargetCom
passes the `BSTR` straight to `p2pf::IP2PHub`, and the facade's parser is the
only thing that ever looks inside it. Four language boundaries, one grammar.

## Caveats carried over

The same caveats as the other rewrites apply and are repeated in each file's
header:

* `LocalInMemoryTest` cannot use `PostP2Pmsg` pump injection — no such thing
  exists above the facade — so it reaches the same end state over Dmx and pays
  one login handshake for it.
* `PipeMsgFactoryTest`'s `RedirectFactory` API is gone; `Send` with a `VARIANT`
  replaced the whole family.
* `RouteLoopbackTest`'s original was never an MSCS harness.
* `Com232MeshTest` needs a com0com null-modem pair on COM5/COM6
  (`setupc install PortName=COM5 PortName=COM6`) and reports SETUP without one.
* **`AlexInterop` loses its reason for existing for the third time.** It is the
  Linux port's Phase-3 exit criterion; the facade rewrite forfeited portability
  by depending on an MFC DLL, the COM rewrite forfeited it again, and this one
  binds through the Windows registry and a CLR callable wrapper. **For the Linux
  port, use the original.**

## The ordering rule still applies

Found while building the Light tree, and it matters most here, because a managed
client is furthest of all from the evidence:

> `OnPeerUp` on the **listening** side can fire **before the dialling side has
> finished logging in**. Sending from it races the handshake, and the far end
> answers an early message with *"Application message received before login /
> Connection dropped out"* — it drops the whole connection.

The first message on a link must come from the side that **dialled**; the
listener answers on receipt. Every harness follows that shape.

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](../LICENSE) for
the full text.

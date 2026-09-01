# ComExamples

Every harness in [`DirectExamples`](../DirectExamples), rewritten a third
time — on **[TargetCom](../../TargetFacade/com)**, the ATL dual-interface COM layer
over the facade.

Same questions, same verdicts, same exit-code contract as
[`FacadeExamples`](../FacadeExamples), so all three trees read
side by side.

```
msbuild "ComExamples(2022).sln" -p:Configuration=Debug -p:Platform=x64
.\run_all.ps1 -IncludeScripts         # register per-user, run everything, unregister
.\run_all.ps1 -Config Release -IncludeScripts
```

**Status: 15/15 on Debug|x64 and 15/15 on Release|x64** (11 C++ harnesses — the
two two-process ones counting each side — plus a PowerShell and a VBScript
client), measured 2026-08-13 against a freshly rebuilt kernel.

`RouteLoopbackTestCom` briefly regressed against that kernel and is green again —
see [downward relay](#downward-relay-and-security_review-m2) below.

Worth noting for this tree specifically: `AlexInteropCom` remains the one
two-process harness that does **not** show the Release teardown fault its Light
and Net twins do — see finding 4 in `dotNetExamples/README.md`.

## Downward relay and SECURITY_REVIEW M2

For a window, `RouteLoopbackTestCom` scenario **[2]** — `Root → Leaf`, straight
down the tree through A — did not arrive:

```
[ERROR] []P2PeerCon::GateAppMsgInbound(this=)
Message source [RouteMesh.Root] is not the logged-in identity [RouteMesh.Root.A]
ADVICE  : Connection dropped out
```

`P2PeerCon::GateAppMsgInbound` (`P2PeerCon.cpp:2015`) is the SECURITY_REVIEW
**M2 source binding**: a message's source had to be the peer's own address **or a
descendant of it** (`IsRable()`). That is right for a link to a descendant and
cannot hold on a link to an **ancestor**, which is this hub's gateway to the rest
of the tree — everything routed down arrives from it still carrying its original
source. The effect was asymmetric, so only one of the four scenarios failed:

| leg | receiving hub's peer | source | then |
|---|---|---|---|
| **[1]** `Leaf → B`, up then down | B's peer is `Root` | `Root.A.Leaf` | passed — below Root |
| **[2]** `Root → Leaf`, down via A | Leaf's peer is `Root.A` | `Root` | **refused** — above A |

**Fixed** (`P2PeerCon.cpp`, 2026-08-13): an ancestor link is admitted without a
source check; descendant and unrelated links are unchanged.

Nothing in the COM layer was ever involved — the same failure appeared in the C++
facade, C# and Java trees, and the fix was one predicate in the kernel.

## Note on TargetCore's flat C API

The numbers above were taken after `TargetCore_c.{h,cpp,_u8.cpp}` — the flat
`extern "C"` / Panama surface — was removed from `TargetCore.dll` on 2026-08-13.
Nothing here used it: `TargetCom.dll` imports only `TargetFacade.dll`, and not
one of the layer's 74 symbols. Removing it changed no result in this tree. The
sources are preserved in `MSCS_JavaBindings\TargetCore\native\`.

`run_all.ps1` registers with `regsvr32 /n /i:user`, i.e. `HKCU\Software\Classes`
only: no elevation, nothing machine-wide, always removed again. It registers the
**staged copy** in `bin\<Config>`, so this directory is self-contained.

## Migrated to TargetFacade ABI 4 — and `IP2PHubCom` was reissued

The facade collapsed its eight typed arming verbs onto **one pair**, and the COM
layer followed:

```
    Listen ( peer, port )            was 8 methods, 3 different signatures
    ListenPipe ( peer, pipeName )
    ListenDmx ( peer, service )
    ListenSerial ( peer, comPort )   ... and the four Connect* to match

    Listen  ( toPeer, endpoint )     is 2 methods, 1 signature
    Connect ( toPeer, endpoint )
```

**`IP2PHubCom` has a new IID** — `{12D65CF2-3F8C-4856-A52F-1B0B35F3B4FE}`. An
interface is immutable once anyone holds it, so reshaping one in place is not
allowed; the honest move is a new identity, and a client built against the old
IID now fails cleanly at `QueryInterface` instead of calling `Listen` with the
wrong argument types. It is a new IID rather than a derived `IP2PHubCom2`
because nothing is deployed to stay compatible with. **Unregister any earlier
build before installing this one** — `run_all.ps1` does that for you, but a
hand-registered copy leaves its old `CLSID`/`Interface` keys behind.

Three consequences worth knowing before you read a log or a diff:

* **Every `X.Server` / `X.Client` pair here is a SIBLING pair**, and the
  surviving verbs classify the pair where the typed ones did not. So most
  harnesses now log one extra line — *"'…Server' and peer '…Client' are neither
  ancestor nor descendant"* — and the arming call returns
  `P2PF_S_UNRELATED_LINK` (`0x0004020C`), which is a **success** code. Every
  check in this tree already used `FAILED()` or `SUCCEEDED()`, so nothing had to
  change to accommodate it. `RouteLoopbackTest` is the one harness whose
  addresses are hierarchical, and it stays silent. As in the Light tree, the
  addresses were deliberately **not** re-based to parent/child: all three trees
  use the same names on purpose.
* **The COM layer no longer range-checks anything.** `Port()`, its private
  helper, is gone — there are no numeric parameters left. A bad port is
  `P2PF_E_ENDPOINT` out of the facade's parser rather than `E_INVALIDARG` out of
  the COM layer. Same mistake, same call, one rule instead of two. Both scripts
  and `Com232MeshTest` assert it.
* **The type library can no longer explain a transport.** Per-transport
  helpstrings (*"Dmx dials do not retry"*) had nowhere to live once four
  transports shared two methods, so that guidance moved into the harness headers
  and the facade README. That is the one thing collapsing the verbs cost, and it
  costs an object-browser reader more than it costs a C++ one.

For an automation caller the trade is otherwise strongly positive, which is the
part worth reading `script\` for: an endpoint is a *value*, so it can come from a
parameter, an INI file or a registry key. Choosing between `ListenDmx` and
`ListenPipe` meant branching on a **method name** at authoring time; choosing
between `"dmx://svc"` and `"pipe://name"` does not. VBScript composes one with
`&`.

## The one thing that actually changed

The messaging is identical to the Light tree. What changed is the **build-time
relationship with the framework**, and that is the whole point of the layer:

| | links | includes | finds the implementation |
|---|---|---|---|
| `DirectExamples` | `TargetCore.lib` + `Msgcore.lib` | four kernel headers, MFC | at link time |
| `FacadeExamples` | `TargetFacade.lib` | one header | at link time |
| `ComExamples` | **nothing** — `ole32`/`oleaut32`/`uuid` | the type library | **in the registry, at run time** |

A COM client has *no* build-time dependency on MSCS at all. That is what makes
VB, VBA, Office, WSH, classic ASP and .NET possible — and the two scripts in
`script\` are there to prove the claim rather than assert it.

## Layout

```
common\Com.props        every build setting: no project lib, MIDL output on the
                        include path, DLL staging
common\ComHarness.h     Apartment / Network / Hub / EventSink / pumping Gate
<Harness>\<Harness>.cpp one file per harness
script\ps_client.ps1    late-bound PowerShell client
script\vbs_client.vbs   late-bound VBScript client (cscript)
bin\<Config>\           all exes plus the four staged DLLs
run_all.ps1             register + build + run + summarise + unregister
```

## Two things a COM client has to get right

**1. It must pump.** Every harness runs **single-threaded-apartment** on
purpose — STA is what the layer's real audience uses, and it is the case
TargetCom's Global-Interface-Table machinery exists for. Hub events are raised
on a dispatch thread inside the DLL and marshalled into this apartment, which
means they arrive **through the message queue**. A plain
`WaitForSingleObject` would hang forever. `com::Gate::wait()` pumps, so every
callback in these harnesses runs on the main thread — visible in the logs, where
each `[deliver]` line carries the same `tid=` as `main`.

**2. Ownership and types.** `BSTR` in, `VARIANT`-wrapped `SAFEARRAY(VT_UI1)`
for payloads, `VARIANT_BOOL` out. `ComHarness.h` absorbs all of it so the
harnesses stay readable, and deliberately uses **nothing but plain COM** to do
it: no ATL, no `_com_ptr_t`, no `#import`, no MFC. If the layer needed a
framework to be usable, that would be worth knowing. It does not.

The result is that the harness bodies are nearly line-for-line the Light ones:

```cpp
p2pf::Network net;                    com::Network net;
auto hub = net.createHub(L"A");       com::Hub hub;  net.createHub(L"A", hub);
hub.onTopic(L"chat", λ);              hub.onTopic(L"chat", λ);
hub.listen(peer, endpoint);           hub.listen(peer, endpoint);
hub.sendText(dst, topic, text);       hub.sendText(dst, topic, text);
```

The endpoint string is the *same string* on both sides of the COM boundary:
TargetCom does not parse it, reformat it or validate it — it `SysAllocString`s
it and hands it to `p2pf::IP2PHub`. So `com::TcpListen` / `TcpDial` / `Pipe` /
`Dmx` / `Serial` in `ComHarness.h` are character-for-character the Light tree's
`light::` composers.

## Lines

Non-comment, non-blank lines:

| | original | Light | COM |
|---|---:|---:|---:|
| eleven harnesses | **2376** | **930** | **841** |
| shared helper | — | 106 | 408 |

The harnesses got *smaller* again (no exception handling, no RAII hub moves),
while the shared helper grew ~5×. That is the honest trade: the COM plumbing did
not disappear, it moved into one place — which is exactly what you want, because
a scripting client never sees `ComHarness.h` at all.

## What each harness adds over its Light twin

Most are a straight port. These four do something the Light version could not:

* **`TwoConTest`** — the duplicate-peer rule crosses out to automation
  **unchanged**, as `0x80040204`, so a VBScript or C# caller can branch on it.
  Adds a PART C for the connection point's own bookkeeping: a stale cookie and
  an unknown source IID both come back `CONNECT_E_NOCONNECTION`.
* **`PipeMsgFactoryTest`** — shows why the payload parameter is a `VARIANT` and
  not a `SAFEARRAY`: C++ passes a struct as bytes, VBScript passes a string, and
  both reach the same `Send`. Anything else is `DISP_E_TYPEMISMATCH`.
* **`Com232MeshTest`** — the port is text inside `serial://COM5` now, so it
  asserts the check that replaced the COM layer's own: `serial://COM0` is
  `P2PF_E_ENDPOINT`, rejected for the same reason `serial://COMx` would be.
  `AlexInterop`, whose port comes from `argv`, is the other half of that story:
  it composes the endpoint from a `LONG` rather than narrowing to
  `unsigned short` first, so `70000` reaches the parser intact instead of
  silently wrapping to 4464.
* **`RouteLoopbackTest`** — four live hub objects, each with its own connection
  point and its own dispatch thread inside the DLL, all marshalling into **one**
  apartment. Also the clearest use of `Broadcast`'s reshaped signature: it
  returns `Delivered` as a value, because the facade's `S_FALSE` ("nobody was
  up") is invisible to an automation client.

The same caveats as the Light tree still apply and are repeated in each file's
header: `LocalInMemoryTest` cannot use `PostP2Pmsg` pump injection (no such
thing above the facade), `PipeMsgFactoryTest`'s factory API is gone, and
`RouteLoopbackTest`'s original was never an MSCS harness. **`AlexInterop` loses
its whole reason for existing twice over** — it is the Linux port's Phase-3 exit
criterion, and COM, the registry, apartments and `BSTR` have no Linux
counterpart. For the Linux port, use the original.

## Events, and the one honest limitation

The C++ harnesses sink `_IP2PHubEvents` through the connection point and get
`OnMessage` / `OnPeerUp` / `OnPeerDown` / `OnError`. So would VB6, or C# with an
interop assembly.

**The scripts do not**, and that is a *host* limitation rather than a gap in the
layer: .NET — and therefore PowerShell — can only bind COM events through an
interop assembly for the coclass, which needs `TlbImp` or an early-bound
reference. WSH can only sink events for objects it created itself via
`WScript.CreateObject(progid, prefix)`, and hubs come from `CreateHub`.

What a script *can* observe end to end is delivery itself, and both scripts use
it: `IsPeerUp` only goes True after the kernel's login handshake completes, and
`Broadcast` returns True only if a peer was up to take a copy. Both are real
proof that traffic moved.

`script\vbs_client.vbs` is deliberately the **least capable client the layer
will ever have** — no byte arrays, no structs, no HRESULT inspection beyond
`Err.Number`. It passes, which is the strongest single statement in this
directory: if it works from `cscript`, it works from VBA, from an Excel macro
and from a logon script.

## The ordering rule still applies

Found while building the Light tree, and it matters *more* here because a COM
client is further from the evidence:

> `OnPeerUp` on the **listening** side can fire **before the dialling side has
> finished logging in**. Sending from it races the handshake, and the far end
> answers an early message with *"Application message received before login /
> Connection dropped out"* — it drops the whole connection.

The first message on a link must come from the side that **dialled**; the
listener answers on receipt. Every harness and both scripts follow that shape.

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](../LICENSE) for
the full text.

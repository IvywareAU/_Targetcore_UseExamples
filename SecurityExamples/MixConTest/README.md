# MixConTest — Test 2: one `P2PeerHub`, mixed `P2PeerConWsa` + `P2PeerConPipe`

**Test 2 question:** can a **single** `P2PeerHub` hold connections of **different transports**
at the same time — a `P2PeerConWsa` (TCP) *and* a `P2PeerConPipe` (named pipe)?

## Answer: **YES** — a hub is transport-agnostic

A hub pumps every connection through the same `P2PeerCon_MAP` regardless of concrete type; the
WSA-vs-pipe difference lives entirely inside the `P2PeerCon` subclass. Nothing in the hub keys on
transport. So one hub can own a live WSA connection and a live pipe connection simultaneously.

## The catch this test uncovered: a hub keys connections by **remote-hub identity**

There is one rule (the same one test 1 met): a hub keeps **at most one connection per remote hub
address**. And crucially, during login a connection's identity (`m_oThatP2Paddr`) is **rewritten to
the peer hub's real address**. So two connections that end up pointing at the *same* remote hub
collide — the second is rejected/closed with:

```
"P2PeerCon with nominated strThatP2Paddr=... already exists"
"Duplicate P2PeerCon's for P2PeerHub attempted"
```

**Consequence for a self-loop:** a first attempt at this test used *one* hub looping back to
*itself* on both transports. It failed — not because of the mix, but because both server-accept
connections resolved to the same self identity (`MixConTest.Hub`) and the second was closed as a
duplicate. Transport type never enters the key, so a WSA con and a pipe con to the *same* remote
hub cannot coexist either.

## Correct design: distinct remote peers (three hubs, one process)

To put a WSA con **and** a pipe con on one hub with distinct identities, they must target
**different** remote hubs:

```
   HubB (WSA  service) <=== TCP  127.0.0.1:7799 ===  HubA   (WSA  client, peer = HubB)
   HubC (Pipe service) <== \\.\pipe\MixConProbe ==   HubA   (Pipe client, peer = HubC)
                                                       ^^^^ the MIXED hub
```

- **HubA** is the subject: it simultaneously owns a `P2PeerConWsa` (identity `HubB`) and a
  `P2PeerConPipe` (identity `HubC`) — distinct identities, no collision.
- HubA is the connector on both, so it receives `On_ConLoginAck` **twice** — once per transport.
  That double-ack is the proof of connectivity.

## Also proven: `PostP2PeerMsg` with a response over both transports

Login only shows the transports are *up*. To prove the mixed hub can carry real application
traffic on each, once a transport's login is acked HubA posts a `P2PeerMsg` (`P2Pmsg_BCast`)
addressed to that transport's peer, and the peer replies:

```
   HubA --request--> HubB  (over the WSA socket)   --response--> HubA
   HubA --request--> HubC  (over the named pipe)   --response--> HubA
```

Every hub overrides `On_P2PeerBCast` to **log** each received message. `HubB`/`HubC` recognise a
request from `HubA` (by source address) and post a response back — addressed to `HubA`, so
`RouteP2PeerMsg` sends it over the very connection the request arrived on. `HubA` logs both
replies; a response returning over **both** the socket and the pipe is the proof that
`PostP2PeerMsg` works — request **and** response — on each transport simultaneously.

Request vs. response is told apart purely by the message **source address** (both reuse the
`P2Pmsg_BCast` id, since that is the id wired to `On_P2PeerBCast` in the hub's `P2PeerMsg_MAP`).

## Verified result (runtime)

Built with MSBuild (`Debug|x64`, VS2022 v143), run headless — **exit code 0 (PASS)**. Trace excerpt
(note the two distinct con pointers HubA owns, then the request/response over each transport):

```
HubA.PostP2PeerCon(WSA  client, that='MixConTest.HubB') -> TRUE
HubA.PostP2PeerCon(Pipe client, that='MixConTest.HubC') -> TRUE
  HubA  On_ConLoginAck  con=...C2C60 addr='MixConTest.HubC'   -> PIPE acked
[HubA ] SEND request  to 'MixConTest.HubC': "Hello HubC over the named pipe"
  HubA  On_ConLoginAck  con=...C29B0 addr='MixConTest.HubB'   -> WSA  acked
[HubA ] SEND request  to 'MixConTest.HubB': "Hello HubB over the WSA socket"
[HubC ] RECV msg from 'MixConTest.HubA': "Hello HubC over the named pipe"
[HubC ] SEND response to 'MixConTest.HubA': "ACK from HubC: got ..."
[HubB ] RECV msg from 'MixConTest.HubA': "Hello HubB over the WSA socket"
[HubB ] SEND response to 'MixConTest.HubA': "ACK from HubB: got ..."
[HubA ] RECV msg from 'MixConTest.HubC': "ACK from HubC: got ..."   <- response over PIPE
[HubA ] RECV msg from 'MixConTest.HubB': "ACK from HubB: got ..."   <- response over WSA (TCP)
VERDICT: PASS -- one hub (HubA) holds a live P2PeerConWsa AND a live P2PeerConPipe,
                 and exchanged a request/response over each
```

## Relationship to Test 1 ([`../../DirectExamples/TwoConTest`](../../DirectExamples/TwoConTest))

- **Test 1** — one hub, two `P2PeerConWsa` (same transport). Passed because it had a *single*
  loopback pair, so only one connection claimed the self identity.
- **Test 2** — one hub, `P2PeerConWsa` + `P2PeerConPipe` (mixed transports). Needs distinct remote
  peers, hence three hubs; the collision that forced this is the same identity rule.

Both confirm: **a hub supervises multiple connections as long as their (post-login) remote-hub
identities are distinct — transport type is irrelevant.**

## Security defaults: two opt-outs this test needs

Both are recent breaking changes in `TargetCore`, both must be configured **before** `SpawnHub()`,
and neither has anything to do with transports. This binary probes connectivity — not who may
speak, nor what may be read — so it takes the documented one-line migration for each.

### 1. `RequireAuth(false)` — a hard gate, and why the test would not start

Peer login authentication is **required by default** (Stage 3 step 8). The very first act of
`SpawnHub()`, before the pump thread exists, is to check the hub can enforce what it requires
(`TargetCore/P2PeerHub.cpp:187`):

```cpp
if ( !AuthArmOrRefuse ( _T(__FUNCTION__) ) )
  return 0;
```

An unprovisioned hub therefore never gets a thread — the alternative being a pump thread that
starts, refuses every peer, and looks healthy from outside. Without the opt-out all three hubs
refuse and `main` exits **1** (`FATAL: SpawnHub() failed.`):

```
[ERROR] []P2PeerHub::SpawnHub()
P2PeerHub(MixConTest.HubA) will not arm: no identity key - SetIdentity(path,true) or ProvisionAuth()
ADVICE  : Auth is required by default (Stage 3 step 8). Provision this hub, or RequireAuth(false) and mean it.
```

`AuthPolicy::Arm()` (`TargetCore/P2PAuthLogin.cpp:660`) refuses one reason at a time and in order —
identity, then allow-list, then that the allow-list loads and names somebody, then revocation — so
provisioning *only* an identity moves the refusal to the next gate rather than clearing it. The
in-tree twin of this test, `MscsUnitTests/mix_con.cpp:221`, takes the same `RequireAuth(false)`
migration on every hub.

Note that `AuthArm()` now reports **`ArmNotRequired`**, deliberately not `ArmOk`: nothing can read
"this hub armed" as "this hub authenticates".

### 2. `RequireSeal(false)` — a warning, not a gate, and inert here

Body sealing is required by default too (Stage 3 step 20), but an unprovisioned hub is **not**
refused. The check sits *inside* the success branch of `AuthArmOrRefuse`
(`TargetCore/P2PeerHub.cpp:2209-2229`, which `return true`s): such a hub still arms, still logs
peers in and still talks to its direct peers; what it cannot do is *open* a body sealed to it, and
a relay is entitled to be in exactly that state. It warns and starts:

```
[ERROR] []P2PeerHub::AuthArmOrRefuse()
P2PeerHub(MixConTest.HubA) requires sealing and holds no agreement key
ADVICE  : It cannot open a body sealed to it. SetAgreementKey(path,true), or RequireSeal(false)
```

Two things worth knowing about that output:

- **It is a warning wearing an error's label.** The code emits `EVWRN`
  (`TargetCore/P2PeerHub.cpp:2221`); the stderr fallback renderer
  (`Msgcore/Msgexception.cpp:1811`) prints `DEBUG` for DEBUG, `TRACE` for TRACE, and `ERROR` for
  *everything else* — WARNING included. The reported caller also changes from `SpawnHub()` to
  `AuthArmOrRefuse()`, which is how to tell the two diagnostics apart at a glance.
- **It could never have affected this test.** `SealAppMsgOutbound`
  (`TargetCore/P2PeerCon.cpp:2773`) returns early when the link is the last hop —
  `if ( m_oThatP2Paddr == strScope ) return true;` — and for a unicast `GetScopeOrDestin()` *is*
  `GetDestin()`. Every message here is single-hop: HubA→HubB rides the con whose far end *is*
  `MixConTest.HubB`. No seal is ever attempted. Nor do these count as broadcasts — `HasScope()` is
  the broadcast test and `TMsg_Scp` is stamped only by `On_P2PeerBCast`/`On_P2PeerUCast` fan-out,
  never by a message *name*, so this test's `P2Pmsg_BCast`-**named** unicasts are untouched by it.

`RequireSeal(false)` is therefore cosmetic here: it buys a run with no diagnostics on stderr,
nothing more.

## Build & run

The tree's [`run_all.ps1`](../run_all.ps1) builds and runs this and its sibling together, and is
the ordinary way in. To drive this one alone:

```
msbuild "MixConTest(2022).vcxproj" /p:Configuration=Debug /p:Platform=x64
# run: ..\out\x64\Debug\MixConTest.exe
```

Like the rest of the repository it references the sibling `Msgcore` and `TargetCore` checkouts
through `..\..\..\` — **three** levels, because this harness sits at
`<repo>\SecurityExamples\MixConTest\` — takes its import libraries from `..\..\..\lib`, and stages
`TargetCore.dll` and `Msgcore.dll` out of `..\..\..\bin\<Config>64` in a post-build step. Nothing
needs copying by hand; that step fails loudly when the DLLs are not there, because `TargetCore` is
delay-loaded and the alternative is `0xC06D007E` at startup with nothing to read. Build `Msgcore`
and `TargetCore` first.

> **Console note.** Unlike `DirectExamples\TwoConTest`, this harness deliberately does **not** put stdout in
> `_O_U16TEXT` mode: with the pipe transport active, wide/narrow stdio writes mix on stdout and a
> `_O_U16TEXT` stream asserts in the UCRT on the first narrow write. Default mode + ASCII output
> avoids it.

## Related

- [`../README.md`](../README.md) — this tree, and why the opt-out version is kept alongside the
  provisioned one rather than superseded by it.
- [`../MixConTestAuth`](../MixConTestAuth) — the same claim, made by hubs that are actually armed.
- [`../../ArchitectureFAQ.md`](../../ArchitectureFAQ.md) — hubs vs pumps, thread affinity, and the
  login handshake.

## Licence

Apache-2.0, as the rest of the repository. See [`LICENSE`](../../LICENSE).

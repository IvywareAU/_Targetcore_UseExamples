# MixConTestAuth — Test 2b: the mixed-transport hub, **provisioned**

`../MixConTest` asks whether one `P2PeerHub` can hold a `P2PeerConWsa` and a `P2PeerConPipe` at the
same time, and answers yes. It does so with the two security defaults switched **off**:

```cpp
    oHub.RequireAuth (false);      // Stage 3 step 8  - a hard arming gate
    oHub.RequireSeal (false);      // Stage 3 step 20 - an outbound gate
```

That is the right call for a binary whose subject is transports — and it means the mixed-transport
claim has only ever been demonstrated on the one posture nobody deploys.

**This binary changes exactly one thing and nothing else: it provisions the three hubs instead of
opting them out.** Same topology, same distinct-identity rule, same request/response proof.

## What that buys

Two things go untested by the opt-out version:

- **the login handshake and the per-connection session cypher**, which ride on `RequireAuth`. They
  are transport-agnostic *only if* the pipe path and the socket path both carry them — an assertion
  until something makes both do it in the same process, in the same run. This does.
- **the arming gate**, which is the part an operator meets first. The interesting failure is not
  "auth broke", it is "auth was never on" — so the pass condition here is `AuthArm() == ArmOk`, and
  deliberately **not** `ArmNotRequired`.

## Topology

Identical to `MixConTest`, with a different address prefix, port and pipe name so both binaries can
run at the same time without fighting over an endpoint:

```
   HubB (WSA  service) <=== TCP  127.0.0.1:7801 ======  HubA   (WSA  client, peer = HubB)
   HubC (Pipe service) <== \\.\pipe\MixConAuthProbe ==  HubA   (Pipe client, peer = HubC)
                                                          ^^^^ the MIXED hub
```

A hub keeps at most **one connection per remote hub identity**, and during login a connection's
`m_oThatP2Paddr` is rewritten to the peer hub's real address — so the two connections must target
*different* remote hubs or the second is closed as a duplicate. Transport type never enters the key.
That is why there are three hubs and not one; `../MixConTest/README.md` has the full argument.

## What "provisioned" means, in the order the gate checks it

`SpawnHub()` calls `AuthArmOrRefuse()` **before the pump thread exists**
(`TargetCore/P2PeerHub.cpp:187`) — the alternative being a pump thread that starts, refuses every
peer, and looks healthy from outside. `AuthArm()` refuses **one reason at a time, in order**, so
satisfying one moves the refusal to the next rather than clearing it:

| `p2pauth::ArmResult` | what is missing |
| --- | --- |
| `ArmNoIdentity` | `SetIdentity()` / `ProvisionAuth()` never succeeded |
| `ArmNoAllowList` | `SetAllowList()` never called |
| `ArmAllowUnusable` | configured, and the last load **failed** — one bad line fails the whole file |
| `ArmEmptyAllow` | loads, parses, and names nobody — the state reached by accident |
| `ArmNoRevocation` | no revocation list, and no `RequireRevocation(false)` to say that was deliberate |
| `ArmRevocationUnusable` | configured and will not load → **fails closed**, refuses every peer |

So `main()` does four things per hub, all **before** `SpawnHub()`:

1. **`ProvisionAuth()`** — creates `<stem>.key` if absent, *finds* it if not, and writes
   `<stem>.key.pub`, the publishable half. Idempotent by design: a first-run helper that regenerated
   on restart would rotate every hub's identity behind the operator's back.
2. **`SetAgreementKey()`** — the **separate** ECDH key others seal *to*. Different container magic
   and different DPAPI entropy from the identity key, so a swapped or renamed file fails loudly
   instead of quietly making a hub sign with the key it agrees with.
3. **`SetAllowList()`** — who this hub will believe. HubA lists HubB and HubC; each service hub lists
   HubA. Three columns, so the peers are also legal sealing destinations. An entry is **not a
   pattern** — `MixConTestAuth.*` in that column admits nothing.
4. **`SetRevocationList()`** — a revocation *position*. The file must **exist**; an all-comments file
   is the honest "nothing revoked yet". The other position, `RequireRevocation(false)`, is one line
   and a real answer for a closed tree — it is shown in the source and deliberately not taken.

The keys persist across runs and **the allow-list is rebuilt on every run**. That asymmetry is the
library's: `ProvisionAuth` finds an existing key, while `AppendAllowList` does not de-duplicate, so a
list appended to on every launch would grow a line per run. It is derived from the keys, so
rebuilding it is free.

Key material lives in `p2p\` **beside the executable**, resolved from `GetModuleFileName` rather than
a fixed absolute path, so a clone of the tree elsewhere just works and a stale key from another build
tree is never picked up silently.

## What `RequireSeal(true)` does — and does not do — here

**Nothing in this test is ever sealed, and that is correct rather than a gap.**
`P2PeerCon::SealAppMsgOutbound` (`TargetCore/P2PeerCon.cpp:2773`) returns early when the link is the
last hop — `if ( m_oThatP2Paddr == strScope ) return true;` — and every message here is single-hop:
HubA→HubB rides the connection whose far end *is* HubB. Sealing engages when a body must cross an
**intermediate** hub, and this topology has none.

What the switch buys is the posture:

- with the agreement keys provisioned, `CanOpen()` is true and the arm-time warning
  (*"requires sealing and holds no agreement key"*) does not fire. That warning is an `EVWRN`, not an
  error, and it prints as `[ERROR]` only because the stderr fallback renderer
  (`Msgcore/Msgexception.cpp:1811`) labels everything that is not DEBUG or TRACE that way. There is
  deliberately **no** `ArmNoAgreement` — a relay legitimately holds no keys, so this is a warning and
  not a gate;
- the allow-lists carry the third column, so if a fourth hub were added and traffic relayed, the seal
  would succeed rather than the send being **refused**. It is refuse, never downgrade: there is no
  path that quietly sends a relayed body in clear because the directory was incomplete.

`RequireSealBroadcast` is likewise left at its default (true) and is likewise inert. `HasScope()` is
the broadcast test and `TMsg_Scp` is stamped only by the `On_P2PeerBCast`/`On_P2PeerUCast` fan-out —
never by a message *name* — so these `P2Pmsg_BCast`-**named** unicasts are untouched by it.

## Exit codes

| code | meaning |
| --- | --- |
| **0** | **PASS** — all three hubs armed with `ArmOk`, HubA completed `On_ConLoginAck` on **both** transports, and received a response over **both** |
| 1 | setup failure (startup / provisioning / factory / `SpawnHub`) |
| 2 | an MFC/CRT assertion fired |
| 3 | a post returned FALSE, a transport did not log in within the timeout, or a response did not come back |
| 4 | **a hub refused to arm** — distinct from 1 on purpose: it is the one failure whose cause is a *file*, and the log names both the reason and the file |

## Reading a failure

A login that fails *after* the transport connected is an **auth** failure, not a transport one. The
three that actually happen:

- `AuthErrUnknownPeer` — the peer is not in this hub's allow-list. Provisioning error.
- `AuthErrSkew` — the login timestamp is outside ±300 s (`SetAuthWindow`). Free in-process; a real
  hazard across a VM resumed from a snapshot or a laptop opened after a week. The window is a
  memory-and-clock parameter, **not** the replay defence — replay is stopped by a per-hub nonce
  cache, and the timestamp only bounds how long that cache must remember.
- `AuthErrRevoked` — listed, *or* the revocation list would not load. It fails **closed**: the safe
  reading of a missing deny-list is never "deny nothing".

One more that is environmental rather than logical: identity files are DPAPI **machine**-scope by
default, so a key file copied from another machine yields `IdErrUnprotect` at load — a named startup
failure, not a mystery. Delete `p2p\` to re-provision from scratch.

## Build & run

The tree's [`run_all.ps1`](../run_all.ps1) builds and runs this and `../MixConTest` together, and
is the ordinary way in. `-Fresh` deletes `p2p\` first, so the **create** path is exercised rather
than the find path — see below. To drive this one alone:

```
msbuild "MixConTestAuth(2026).vcxproj" /p:Configuration=Debug /p:Platform=x64
# run: ..\out\x64\Debug\MixConTestAuth.exe   (keys land beside it, in p2p\)
```

Mirrors `../MixConTest` exactly: references the sibling `Msgcore` and `TargetCore` checkouts
through `..\..\..\` — **three** levels, this harness sitting at
`<repo>\SecurityExamples\MixConTestAuth\` — takes its import libraries from `..\..\..\lib`, and
stages both DLLs from `..\..\..\bin\<Config>64` in a post-build step that fails loudly when they
are missing.

> **Console note.** Like `../MixConTest` and unlike `DirectExamples\TwoConTest`, stdout is deliberately left in its
> default translated mode. With the pipe transport active, wide and narrow stdio writes mix on
> stdout, and a stream in `_O_U16TEXT` asserts inside the UCRT on the first narrow write. All output
> here is ASCII.

## Relationship to the other tests

- [**`../../DirectExamples/TwoConTest`**](../../DirectExamples/TwoConTest) — one hub, two
  `P2PeerConWsa`. Same transport, single loopback pair.
- [**`../MixConTest`**](../MixConTest) — one hub, `P2PeerConWsa` + `P2PeerConPipe`, security opted
  **out**.
- **`MscsUnitTests/mix_con.cpp`** — the in-tree twin of `MixConTest`, and it takes the same
  `RequireAuth(false)` migration. Not part of this repository.
- **this** — the same mixed-transport claim, made by hubs that are actually provisioned.

[`../README.md`](../README.md) is the tree, and says why the opt-out version is kept alongside this
one rather than superseded by it: the pair is a differential, and a differential needs both terms.

## Licence

Apache-2.0, as the rest of the repository. See [`LICENSE`](../../LICENSE).

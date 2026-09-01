# `SecurityExamples` — the same claim, made twice, with the security defaults **off** and then **on**

The five binding trees in this repository vary *how a caller reaches* TargetCore
and hold the subject fixed. This tree does neither. It holds the **binding**
fixed — both harnesses are plain C++ against `TargetCore.lib`, exactly as
`DirectExamples` is — and varies the **security posture** instead.

That is the whole design, and it is worth stating plainly because it is what
makes the pair worth more than either half:

| | Harness | Posture | Asks |
| - | ------- | ------- | ---- |
| 1 | [`MixConTest`](MixConTest) | `RequireAuth(false)`, `RequireSeal(false)` | can **one** `P2PeerHub` hold a `P2PeerConWsa` **and** a `P2PeerConPipe` at once? |
| 2 | [`MixConTestAuth`](MixConTestAuth) | provisioned — `RequireAuth` and `RequireSeal` left at their defaults | the same question, of hubs that are actually **armed** |

Same topology, same distinct-identity rule, same request/response proof. The
second changes exactly one thing about the first: it provisions the three hubs
instead of opting them out.

## Why both, and not just the second one

The obvious reading is that `MixConTestAuth` supersedes `MixConTest` — it proves
more, on the posture people actually deploy, so why keep the weaker one? Because
the pair is a **differential**, and a differential needs both terms.

* `MixConTest` isolates the transport claim. When it passes and `MixConTestAuth`
  fails, the mixed-transport machinery is fine and the failure is in
  provisioning — a key, an allow-list, a clock. That is a different afternoon's
  work, and knowing which afternoon it is before you start is most of the value.
* `MixConTestAuth` alone could pass for the wrong reason. A hub that never armed
  is a hub that refuses every peer, and a harness that measured only "did the
  transports come up" would not notice. So its pass condition is
  `AuthArm() == ArmOk` and deliberately **not** `ArmNotRequired`, which is what
  `MixConTest`'s hubs report.

The failure the pair exists to catch is not "auth broke". It is **"auth was
never on"** — and a single harness, whichever posture you picked for it, cannot
see that.

## What the second one covers that the first cannot

Two things ride on `RequireAuth`, and both go untested by an opt-out:

* **the login handshake and the per-connection session cypher.** They are
  transport-agnostic *only if* the pipe path and the socket path both carry them
  — which is an assertion until something makes both do it in the same process,
  in the same run. `MixConTestAuth` does.
* **the arming gate**, which is the part an operator meets first. `SpawnHub()`
  calls `AuthArmOrRefuse()` **before the pump thread exists**, the alternative
  being a pump thread that starts, refuses every peer, and looks healthy from
  outside.

## The rule both harnesses run into, and it is not a security rule

A hub keeps **at most one connection per remote hub identity**, and during login
a connection's `m_oThatP2Paddr` is rewritten to the peer hub's real address.
Transport type never enters that key. So a `P2PeerConWsa` and a `P2PeerConPipe`
aimed at the *same* remote hub collide, and the second is closed as a duplicate:

```
"P2PeerCon with nominated strThatP2Paddr=... already exists"
"Duplicate P2PeerCon's for P2PeerHub attempted"
```

That is why both harnesses stand up **three** hubs in one process rather than
looping one hub back to itself, and it is worth knowing before reading either:
the first attempt at `MixConTest` did exactly that and failed, and the failure
had nothing to do with mixing transports.

```
   HubB (WSA  service) <=== TCP  127.0.0.1 ===  HubA   (WSA  client, peer = HubB)
   HubC (Pipe service) <== named pipe =========  HubA   (Pipe client, peer = HubC)
                                                   ^^^^ the MIXED hub
```

The two harnesses use **different** ports and pipe names — 7799 and
`MixConProbe` against 7801 and `MixConAuthProbe` — so a leftover instance of one
cannot fail the other.

## Exit codes

`MixConTest` uses the repository-wide four. `MixConTestAuth` adds a fifth, and
the addition is the point of the tree:

| code | meaning |
| ---- | ------- |
| `0` | PASS |
| `1` | setup failure (startup / factory / connect) |
| `2` | an MFC/CRT assertion fired |
| `3` | a check failed, or nothing arrived before the timeout |
| `4` | **a hub refused to arm** — `MixConTestAuth` only |

`4` is separated from `1` deliberately. Every other setup failure is a bug in
the harness; this one is a **file** — a missing identity key, an allow-list that
names nobody, a revocation list that will not load — and the log names both the
reason and the path. Read as an ordinary setup failure it sends you looking in
the wrong place.

## Key material

`MixConTestAuth` provisions six keys — an identity and an agreement key per hub —
into `p2p\` **beside its executable**, resolved from `GetModuleFileName` rather
than from the working directory, so a clone of the tree elsewhere just works and
a stale key from another build tree is never picked up silently. With this tree's
output root that is `out\x64\<Config>\p2p\`, which `.gitignore` already covers.
A private key is machine-scoped DPAPI material; it is useless anywhere else and
must never be committed.

`ProvisionAuth()` is **idempotent**: it creates the key if absent and *finds* it
if not, because a first-run helper that regenerated on restart would rotate every
hub's identity behind the operator's back. The allow-list, by contrast, is
rebuilt on every run — `AppendAllowList` does not de-duplicate, so a list
appended to on every launch would grow a line per run, and it is derived from the
keys anyway. That asymmetry is the library's, not the harness's.

The practical consequence for running this tree: the **second** run exercises the
find path, not the create path. `run_all.ps1 -Fresh` deletes `p2p\` so the create
path is covered too. Run it both ways at least once.

## Building and running

```powershell
.\run_all.ps1                    # build + run Debug
.\run_all.ps1 -Config Release
.\run_all.ps1 -NoBuild
.\run_all.ps1 -Fresh             # re-provision from scratch
```

`run_all.ps1` builds `SecurityExamples(2022).sln`, runs both harnesses, prints a
verdict table and **exits with the number of failures**. Neither harness is
interactive and neither needs elevation, so — unlike `DirectExamples` and
`ErrorReportingExamples` — nothing in this tree has to be skipped.

Both harnesses **delay-load** `TargetCore.dll` and stage it, with `Msgcore.dll`,
from `..\..\..\bin\<Config>64` in a post-build step. That step fails loudly when
the DLL is not there, because the alternative is `0xC06D007E` at startup with
nothing to read. Build `Msgcore` and `TargetCore` first.

> **Console note.** Unlike `DirectExamples\TwoConTest`, neither harness puts
> stdout into `_O_U16TEXT`. With the pipe transport active, wide and narrow
> stdio writes mix on stdout, and a stream in `_O_U16TEXT` asserts inside the
> UCRT on the first narrow write. All output here is ASCII.

## Related

* [`DirectExamples/TwoConTest`](../DirectExamples/TwoConTest) — one hub, two
  `P2PeerConWsa`. Same transport, a single loopback pair, and the same identity
  rule met from the other side.
* [`ArchitectureFAQ.md`](../ArchitectureFAQ.md) — hubs vs pumps, thread
  affinity, and the login handshake, at the root because it holds for every tree.
* `TargetCore/SECURITY.md` — the posture table these two harnesses sit either
  side of, and the documented one-line migration each of them takes.
* `MscsUnitTests/mix_con.cpp` — the in-tree twin of `MixConTest`, which takes the
  same `RequireAuth(false)` migration. Not part of this repository.

## Licence

Apache-2.0, as the rest of the repository. See [`LICENSE`](../LICENSE).

# RouteLoopbackTest

A single-process, **no-wire** routing example for the **treehub_runtime**
engine — the clean-room in-process router the code generator emits generated
projects against. It spotlights the one function that actually moves a message
over the DIRECT loopback transport:

```cpp
PeerNetwork::route(origin, dst, body);
```

> **Not an MSCS/TargetCore harness.** Unlike its siblings in this folder
> (`LocalInMemoryTest`, `PipeMeshTest`, `WsaMeshTest`, …), this example uses no
> `P2PeerHub`, no `PostP2Pmsg`, no pump thread. It links the prebuilt
> `treehub_runtime.lib`, which is produced by a separate repository that this
> tree deliberately does not vendor.

## The mechanism

**DIRECT loopback = in-process copy-on-deliver.** `route()` walks the
explicitly-wired parent↔child connection graph **hop by hop, on the calling
thread**, invoking each node's hop hook in-process — no sockets, no named
pipes, no byte framing, no threads:

```cpp
TypedTransportPort transport(TypedTransportPort::ConnectionType::DIRECT);
PeerNetwork        net(transport);

PeerNode root(net.allocateHubId(), "VNet1:Root",   net);
PeerNode a   (net.allocateHubId(), "VNet1:Root.A", net);
// ... listen(port) on each, then connectTo() both ways per edge ...

net.route(&leaf, PeerAddress("VNet1:Root.B"), "hello|from the leaf");
```

`PeerNode::sendText`, `HubMesh::send`, and the generated-DLL
`MeshHandle::send` are all thin wrappers that build the destination address and
call `route()` for you.

## The mesh

Four hubs in a two-level tree (kernel ids from 1000):

```
             Root (:1000)
            /            \
         A (:1001)      B (:1002)
         |
      Leaf (:1003)
```

## What it checks

Each node's `onTransit` / `onDeliver` / `onDropped` hooks record what `route()`
did, so the verdict asserts the exact hop sequence — not just "arrived":

| # | `route()` call | Path | Result |
|---|----------------|------|--------|
| 1 | Leaf → `Root.B` | Leaf → A → Root → B (up then down, 2 transit hops) | `DELIVERED` |
| 2 | Root → `Root.A.Leaf` | Root → A → Leaf (down only, 1 transit hop) | `DELIVERED` |
| 3 | Leaf → `Root.Z` | up to Root, no child subtree contains dst | `DROPPED_NO_ROUTE` |

Routing decisions are **segment-aware** (`PeerAddress` splits on `.` and
compares segment vectors), so `Root.A` is never mistaken for an ancestor of
`Root.AB`.

## Build & run

Needs the prebuilt `treehub_runtime.lib`, which is produced by a separate
repository this tree does not vendor. Build the native libs there first:

```
<treehub-runtime-repo>\native\build.ps1 -NoTest                  # Debug
<treehub-runtime-repo>\native\build.ps1 -NoTest -Config Release  # Release
```

Then open the tree-wide `..\DirectExamples(2026).sln` in Visual Studio 2026
(v145, x64), or from a VS developer prompt:

```
msbuild "RouteLoopbackTest(2026).vcxproj" /p:Configuration=Debug /p:Platform=x64
..\out\x64\Debug\RouteLoopbackTest.exe
```

Both configurations build and pass. Note that `build.ps1` is per-configuration:
a Release build of this harness needs the Release `treehub_runtime.lib`, and
linking one CRT against the other is the failure mode to expect if you skip it.

The project reads those paths from `KgnRoot`, which has **no default**. Set the
`KGN_ROOT` environment variable, or pass `/p:KgnRoot=<path>` to msbuild; it must
point at the `native` directory that holds:

- **Include:** `$(KgnRoot)\treehub_runtime\include`
- **Lib:** `$(KgnRoot)\build\x64-debug\$(Configuration)`
  — one multi-config CMake tree, despite the `x64-debug` name, holding both
  `Debug\` and `Release\`.

An unset `KgnRoot` fails with a named error rather than a missing-header
cascade. It was hardcoded to one machine's checkout until 2026-09-02, so the
project built nowhere else.

C++20 is required throughout, and the CRT must match how `treehub_runtime.lib`
is compiled: `/MDd` in Debug, `/MD` in Release.
There are no runtime DLL dependencies (static lib, no MSCS, no post-build copy).

## Verdict (process exit code)

| Code | Meaning |
|------|---------|
| 0 | SUCCESS — all three scenarios routed exactly as expected |
| 3 | FAIL — a route delivered/dropped differently than expected |

## Relation to the other harnesses

- `LocalInMemoryTest` — MSCS TargetCore; two in-process hubs, **no wire**, via `PostP2Pmsg` pump-injection.
- `PipeMeshTest` / `WsaMeshTest` — MSCS; real named-pipe / loopback-TCP cons, two hubs in one process.
- `AlexTest` — MSCS; real WSA con across **two processes**.
- **`RouteLoopbackTest` — treehub_runtime (non-MSCS); pure `PeerNetwork::route()` hop-by-hop, no threads.**

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](../../LICENSE) for
the full text.

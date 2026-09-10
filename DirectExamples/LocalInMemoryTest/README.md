# LocalInMemoryTest

A single-process, **two-hub in-memory** delivery example for TargetCore.

Two `P2PeerHub`s live in one process, each on its own `SpawnHub()` pump
thread, and exchange `P2PeerMsg`s **without any connection between them** — no
socket, no named pipe, no login handshake, no `P2PeerCon` at all.

## The mechanism

```cpp
P2PeerMsg32* pMsg = new P2PeerMsg32(srcAddr, dstAddr, P2Pmsg_BCast, data, bytes);
PostP2Pmsg(pMsg, targetHub.GetHubID());   // drop it on the target hub's pump queue
```

Each hub's `SpawnHub()` pump drains its own `P2Pmsg` queue in the same loop as
its IOCP. `PostP2Pmsg(msg, hubId)` injects a message straight onto a named
hub's pump queue; that pump dispatches it, and because the message's
destination address equals the target hub's own address, the pump fires
`On_P2PeerBCast` on that hub — exactly as if it had arrived over a wire.

This is the lightest way to realise a multi-hub mesh in one address space: it
skips the entire transport/handshake layer that `P2PeerConWsa` (loopback TCP)
and `P2PeerConPipe` (named pipe) exercise. Use it when every hub is in-process
and you only need routing/dispatch semantics, not a real network.

## Threading rule

`PostP2Pmsg(msg, otherHubId)` is only legal from a **non-hub thread** (here,
`main`). Calling it from inside one hub's pump thread to target a *different*
hub trips a cross-hub-context `ASSERT` in the kernel. All sends in this example
originate on the main thread.

## Build & run

Open the tree-wide `..\DirectExamples(2026).sln` in Visual Studio 2026 (v145,
x64 Debug) and build this project, or from a VS developer prompt:

```
msbuild "LocalInMemoryTest(2026).vcxproj" /p:Configuration=Debug /p:Platform=x64
x64\Debug\LocalInMemoryTest.exe
```

The solution references the sibling `Msgcore` and `TargetCore` projects and
links their `.lib`s from `..\..\..\lib`; the post-build step copies the
`Msgcore` DLLs next to the exe. `TargetCore.dll` must be reachable at
run time (it lives in `%WDMSCS_DEBUG%`).

## Verdict (process exit code)

| Code | Meaning |
|------|---------|
| 0 | SUCCESS — both hubs received the BCast addressed to them |
| 2 | ASSERT — an MFC/CRT assertion fired (banner printed) |
| 3 | TIMEOUT — delivery did not complete in time |
| 1 | SETUP — startup failure |

## Relation to the other harnesses

- `AlexTest` — real WSA (loopback TCP) connection, **two processes**.
- `WsaMeshTest` — real WSA connection, two hubs in **one** process.
- `PipeMeshTest` — real named-pipe connection, two hubs in one process.
- **`LocalInMemoryTest` — no connection at all**; pure `PostP2Pmsg`
  pump-injection between two in-process hubs.

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](../../LICENSE) for
the full text.

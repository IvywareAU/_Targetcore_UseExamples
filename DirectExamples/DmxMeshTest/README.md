# DmxMeshTest

A single-process, **two-hub in-process CONNECTION** example using `P2PeerConDmx`
— DMX (Direct Memory eXchange), the in-address-space transport.

Unlike [`LocalInMemoryTest`](../LocalInMemoryTest) (which bypasses connections
entirely and injects straight onto a hub pump via `PostP2Pmsg`), this harness
drives a **real `P2PeerCon`**: two `P2PeerHub`s in one process establish a
`P2PeerConDmx` service⇄client connection, run the full Targetcore **login
handshake**, and exchange a BCast over the connection — with no socket, no
named pipe, and no OS handle. Endpoints are matched by a shared **service name**
string via a process-global registry (`g_oCListP2PeerConDmx`).

It mirrors [`PipeMeshTest`](../PipeMeshTest) exactly, with the transport swapped
from `P2PeerConPipe` to `P2PeerConDmx`.

## This example exists because it exposed (and validates the fix for) two bugs

`P2PeerConDmx` had never actually worked. Building this harness against the
stock lib produced `On_ConStartup → On_ConAccept` then an immediate
`On_ConClose` of the accepted connection, no login. Two defects in
`Targetcore` were fixed (see the commit / the `mscs-inprocess-mesh` note):

1. **`P2PeerConDmx::OnAccept()` never armed the accepted con's receive.**
   `AcceptSpawn()` clears `ConState_Recv|Send` and leaves the spawn with no
   outstanding IO; the Dmx override only called `SetState()` and never created
   or **posted** the accept con's `m_pOVERLAPPEDrecv` (the way
   `P2PeerConPipe::OnAccept()` does). Without a posted recv the client's login
   send could never rendezvous. Fixed by arming + posting the recv (and
   guarding a latent null-deref).

2. **`P2PeerioDmx::RecvP2PeerMsg()` byte-count guard was always true.**
   The guard was `(uiSync1 & 0x00FFFFFF) + sizeof(oSync) > dwBytes`. But
   `uiSync1 & 0x00FFFFFF` is the **total** image size (incl. the 8-byte sync
   header — see `MsgVBHeap.cpp`), and the sender sets
   `dwBytes = P2Piomage_Sizeof()` which returns *exactly that field*. So the
   test reduced to `X + 8 > X` — always true — and every received Dmx message
   was rejected as "Corrupted VBListIOmage byte count". Fixed by dropping the
   spurious `+ sizeof(oSync)` (this is the sibling of the `MsgVBHeap`
   off-by-`sizeof(oSync)` bug fixed earlier).

With both fixes the handshake completes: `On_ConLogin → On_ConLoginAck →`
client BCast `→` server `On_P2PeerBCast`.

## Build & run

Open the tree-wide `..\DirectExamples(2026).sln` in Visual Studio 2026 (v145,
x64 Debug) and build this project, or from a VS developer prompt:

```
msbuild "DmxMeshTest(2026).vcxproj" /p:Configuration=Debug /p:Platform=x64
x64\Debug\DmxMeshTest.exe
```

The solution references the sibling `Msgcore` and `Targetcore` projects and
links their `.lib`s from `..\..\..\lib`; the post-build copies the `Msgcore`
DLLs **and** `Targetcore.dll` (from `%WDMSCS_DEBUG%`) next to the exe, so
it runs standalone.

> After changing `P2PeerConDmx.cpp` / `P2PeerioDmx.cpp`, rebuild `Targetcore`
> first, then copy `Targetcore\x64\Debug\Targetcore.dll` to
> `%WDMSCS_DEBUG%` (`..\..\..\bin\Debug64`) — the `.lib` auto-updates in `..\..\..\lib`
> but the DLL does not.

## Verdict (process exit code)

| Code | Meaning |
|------|---------|
| 0 | SUCCESS — client got `On_ConLoginAck` AND server received the BCast |
| 2 | ASSERT — an MFC/CRT assertion fired (banner printed) |
| 3 | TIMEOUT — handshake/delivery did not complete in time |
| 1 | SETUP — startup/factory failure |

## Relation to the other harnesses

- `AlexTest` — real WSA (loopback TCP) connection, **two processes**.
- `WsaMeshTest` — real WSA connection, two hubs in one process.
- `PipeMeshTest` — real named-pipe connection, two hubs in one process.
- `LocalInMemoryTest` — **no** connection; pure `PostP2Pmsg` pump-injection.
- **`DmxMeshTest`** — real `P2PeerConDmx` in-process connection (this project).

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](../../LICENSE) for
the full text.

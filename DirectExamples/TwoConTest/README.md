# TwoConTest — one `P2PeerHub`, two `P2PeerConWsa` connections

**Test 1 question:** create two connections (`P2PeerConWsa::ServiceFactory` and
`P2PeerConWsa::ClientFactory`), create a **single** `P2PeerHub`, and hand both to the same hub:

```cpp
hub.PostP2PeerCon(ConServer);   // service (listens)
hub.PostP2PeerCon(ConClient);   // client  (connects)
```

Is it possible to use two connections with one hub?

## Answer: **YES** — with one rule

A `P2PeerHub` owns a *list* of connections (`EnumP2PmsgCon`), so it is built to supervise many at
once. The single constraint, enforced in `P2PeerHub::PostP2PeerCon` (`TargetCore/P2PeerHub.cpp:432-447`):

> a connection is rejected (`PostP2PeerCon` returns `FALSE`) if its identification address —
> `P2PeerCon::GetP2Paddress()`, i.e. `m_oThatP2Paddr`, the **remote-peer address** — duplicates one
> already posted to that hub.

So **two (or more) connections coexist under one hub as long as they carry distinct peer
addresses.** Give both the same `strP2PaddrThat` and the second post is deduped away.

## What this harness does

Single process, single hub (`TwoConTest.Hub`), TCP loopback on `127.0.0.1:7788`:

- **PART A (positive):** posts a Service con (`that = PeerA`) and a Client con (`that = PeerB`) —
  distinct addresses — to the same hub. Both posts return `TRUE`; over loopback the hub connects
  to itself and the client con reaches `On_ConLoginAck`.
- **PART B (negative):** posts a third con whose `that` address duplicates `PeerA`. The post
  returns `FALSE` (correctly rejected).

Verdict is the process **exit code**: `0` = PASS, `2` = assert, `3` = behaviour differed from the
source, `1` = setup failure.

## Verified result (runtime)

Built with MSBuild (`Debug|x64`, VS2022 v143) and run headless — **exit code 0 (PASS)**. The trace
shows one hub concurrently driving **three** `P2PeerCon` objects (note the distinct `con=` pointers):

```
PostP2PeerCon(ConServer, that='TwoConTest.PeerA') -> TRUE (accepted)
  HUB  On_ConStartup   con=...5920 addr='TwoConTest.PeerA'   <- listening con
  HUB  On_ConListen    con=...5920 addr='TwoConTest.PeerA'
PostP2PeerCon(ConClient, that='TwoConTest.PeerB') -> TRUE (accepted)
  HUB  On_ConStartup   con=...F020 addr='TwoConTest.PeerB'   <- client con
  HUB  On_ConConnect   con=...F020 addr='TwoConTest.PeerB'
  HUB  On_ConAccept    con=...5920 addr='TwoConTest.PeerA'
  HUB  On_ConAccept    con=...C780 addr='TwoConTest.PeerA'   <- accept-spawned con
  HUB  On_ConLogin     con=...C780 addr='TwoConTest.PeerA'
  HUB  On_ConLoginAck  con=...F020 addr='TwoConTest.PeerB'   <- loopback handshake done
PART B: PostP2PeerCon(ConDup, that='TwoConTest.PeerA') -> FALSE (correctly rejected)
VERDICT: PASS
```

## Build & run

The project references the sibling `Msgcore` and `TargetCore` projects (`..\..\lib` for import libs,
`..\..\Msgcore` / `..\..\TargetCore` for headers), mirroring `AlexTest`.

```
msbuild "TwoConTest(2022).vcxproj" /p:Configuration=Debug /p:Platform=x64
# run: x64\Debug\TwoConTest.exe   (needs TargetCore.dll + Msgcore.dll beside it)
```

> Runtime note: `TargetCore.dll` is delay-loaded. The post-build step copies it from
> `..\..\TargetCore\x64\Debug\`; if you build `TargetCore` only to `..\..\bin\Debug64\`, copy the DLL
> next to `TwoConTest.exe` manually (as this run did).

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](../../LICENSE) for
the full text.

# ExplorerTest

The **hub Explorer** — Targetcore's directory service — asked what a hub is,
three times, by a client that was given its identity rather than choosing one.

Two `P2PeerHub`s in one process over loopback TCP, exactly like
[`WsaMeshTest`](../WsaMeshTest), plus the one thing that harness has not got:
an **expump** on the server hub, and a client that speaks `P2PexpumpCtrl` to
it.

```
msbuild "ExplorerTest(2026).vcxproj" /p:Configuration=Debug /p:Platform=x64
..\out\x64\Debug\ExplorerTest.exe [port]        # default 7823
```

Verdict is the exit code: **0** success, **1** setup, **2** an assert fired,
**3** timeout/incomplete.

## What a run looks like

```
[CLIENT] login acknowledged: this='CEX.XC0' that='CEX' (ack 4 bytes)
[CLIENT] --> P2PexpumpCtrl { QHub } from 'CEX.XC0'
[CLIENT] <-- P2PexpumpHub #1  [CEX] -> [CEX.XC0]
           Machine                  DESKTOP-S2QLPSJ
           Executable               ...\out\x64\Debug\ExplorerTest.exe
           {P2PeerHub}              Null
           PumpsMax                 0
           Name                     CEX
           P2Paddr                  CEX
           P2Padom                  CEX
           {P2PeerTarget}           Null
           Name
```

Nine nodes: `Machine` and `Executable`, which `NotifyP2PmsgExp_Hub`
(`P2Pwin32.cpp:1348-1386`) adds with descriptions, and then the hub's own
`Serialise(0)` appended whole. **The answer is the hub's property tree**, so it
grows with whatever the hub is carrying.

## The command vocabulary

One `P2PexpumpCtrl` message carries **one** command, as a named field on its
data node. The hub verbs:

| Command | Effect |
|---|---|
| `QHub` | ask once; a `P2PexpumpHub` comes back addressed to you |
| `RHub` | the same answer, **and** register as a standing status sink |
| `DHub` | de-register; no answer |

`RCon`/`QCon`/`DCon` and `RPmp`/`QPmp`/`DPmp` are the same three verbs over
connections and pumps. This example drives `QHub`, `QHub@Dsc`, `RHub` and
`DHub`, and asserts that the first three are answered and the fourth is not.

**One command per message.** `On_P2PexpCtrl` (`P2PeerExplorer.cpp:1450-1601`)
loops over the data node's cursor but re-tests every command with `Exists()`
against the *whole* node on each pass — so a message carrying two commands
executes the first one twice and the second one never.

## Five things that are not like the other harnesses

1. **The Explorer is not automatic.** `P2PeerHub.cpp:1034-1040` states that
   instantiating one is deliberately the *application's* job, and the
   message-driven `CREATE`/`SPAWN`/`CLOSE` path in the hub is commented out in
   its entirety. `P2PeerExpump_ACTIVATE()` is the way in: it constructs, spawns
   **and registers with the hub**, and all three matter — an expump the hub
   cannot see is an orphan nothing routes to. Pass no instance for a stock
   Explorer, or your own subclass to have it adopted.

2. **The service connection is posted to the expump**, not to the hub —
   `pExp->PostP2PeerCon()` — and its address is the **slot domain**,
   `CEX.XC*`, the wildcard the Explorer fills in one `XCid` at a time.

3. **The client must be anonymous.** `On_XCidConLogin`
   (`P2PeerExplorer.cpp:1302`) throws *"Null P2Paddr expected"* at any client
   that declares an address of its own, then server-assigns `<hub>.XC%i` from a
   free slot. A peer cannot choose its Explorer identity, by design — and
   registrations are bound back onto the assigned address, so it cannot
   register a fabricated descendant of it either.

4. **The client must not delegate `On_ConLoginAck` to the base.**
   `P2PeerTarget::On_ConLoginAck` (`P2PeerTarget.cpp:2552-2557`) throws on a
   non-empty acknowledgement, and the Explorer acknowledges with the slot index
   (`:1305`) — so the *stock* handler cannot complete an Explorer login at all.
   The override here does what the base does minus that check. That is the
   current price of admission, and it is why the progress log says there is no
   stock client that can log into an Explorer.

5. **The answer arrives through `PeekP2PeerMsg`, and must be consumed.** The
   pump peeks ahead of the map dispatch for any message addressed to this hub
   (`P2Pwin32.cpp:3112-3115`), and after the LoginAck this hub *is* `CEX.XC0`.
   Returning anything but `msgHANDLED` reflects the message back as an
   exception, which `On_P2PmsgExp_CATCH` reads as a dead sink and
   de-registers — so a client that ignores the answer silently unsubscribes
   itself.

## Two things measured here, both worth knowing before you copy this

**`@Dsc` is parsed and then ignored — on the hub answer.** The qualifier is
written by passing `TRUE` for `P3PmsgField_SERIALISE`'s `bDscAttr`, which hangs
a `Dsc` attribute off the command field. `On_P2PexpCtrl` reads it (`:1462`) and
passes it to `QueryP2PmsgExp_Hub` as `bVerbose` — which that function does not
use (`P2Pwin32.cpp:1388-1407`, where the parameter is even renamed
`bRegister`). Measured: `QHub` and `QHub@Dsc` both return the same 9 nodes.
The example prints both counts and says so, and will say so louder if that ever
stops being true.

**The hub is called `CEX` because the library still has that name in it.**
`P2PeerExplorer::PeekP2PeerMsg` (`P2PeerExplorer.cpp:935`) widens its
interception address for a `P2PexpumpHub` only when `GetP2PaddrHub()=="CEX"`,
and the author left a `TODO` there saying it should persist properly. Two lines
below, `:945-951` builds `CEX.XC*` and `CEX.XC0` literals for an `ASSERT`.
`MscsUnitTests\p2p_expreg.cpp` uses `CEX` for the same reason. Renaming the hub
is untested ground; an example is the wrong place to find out.

## What this example deliberately does not do

**It does not subclass `P2PeerExplorer`.** The server runs a stock one.
Overriding is for an application that wants to watch the registry or raise its
own notifications — and nothing in Targetcore raises a hub-status *broadcast*
on its own (`m_dwExpumpMask` is written at `P2Pwin32.cpp:1137` and read
nowhere), so the only way to see the broadcast half of `On_P2PmsgExp_Hub` is to
call `QueryP2PmsgExp_Hub(L"", …)` from inside the expump thread.
[`MscsUnitTests\p2p_expreg.cpp`](../../../MscsUnitTests/p2p_expreg.cpp) does
exactly that, and is the place to look next: it is the security gate test for
the same feature — forged registrations, reaping on disconnect, delivery — and
it overrides `On_P2PexpCtrl` and `On_XCidConClose` to sample the registry from
the only thread that mutates it.

## Prerequisites

`Msgcore.dll` and `Targetcore.dll` must be **current** in `..\..\..\bin\<Config>64`
before this links, and the Explorer is the sharpest possible way to discover
they are not: `P2PeerExpump_ACTIVATE` grew a second parameter in `de4fb9a`, so
an import library built before that commit fails to resolve it. The post-build
step stages both DLLs and fails loudly if `Targetcore.dll` is missing, because
it is delay-loaded and the alternative is `0xC06D007E` at startup.

```
msbuild "..\..\..\Targetcore\Targetcore(2026).vcxproj" /p:Configuration=Debug /p:Platform=x64
```

## Relation to the other harnesses

- `WsaMeshTest` — the same two-hub loopback-TCP arrangement, no expump. Start
  there; if it is red, this cannot pass.
- `AlexTest` / `AlexInterop` — the two-process ancestors of the tree.
- **`ExplorerTest` — the same wire, plus the Explorer: activation, XCid
  assignment, the three hub commands, and the directory that comes back.**

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](../../LICENSE) for
the full text.

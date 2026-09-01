# PipeMsgMapTest — two hubs, a named pipe, and `P2PeerMsg_MAP` routing

Builds on [`PipeMeshTest`](../PipeMeshTest): same single-process, two-`P2PeerHub`,
named-pipe (`P2PeerConPipe`) transport — but instead of catching the delivery with a
virtual `On_P2PeerBCast` override, this harness routes **application-defined** messages
through a `P2PeerMsg_MAP` and performs a **request → response** round trip.

```
client --HubPing--> [named pipe] --> server.On_HubPing
server --HubPong--> [named pipe] --> client.On_HubPong   => DONE
```

> **See also:** [`PipeMsgFactoryTest`](../PipeMsgFactoryTest) — the same round trip with the
> two send sites rewritten to originate the named message through the framework's *factory*
> API (`RedirectFactory`) instead of a hand-built `P2PeerMsg32`. It also documents why
> `ResponseFactory` is **not** a safe standalone cross-hub reply (it inherits the received
> message's routing prefix and loops back into the current hub).

## The routing rule this exercises

> The `P2PeerMsg_MAP` is supported by any object derived from `P2PeerTarget`. Any
> `P2PeerMsg` for which the destination address matches the hub address is pumped through
> the hub `P2PeerTarget` hierarchy until a matching handler is located.

Both hubs are one class, `PipeMapHub : public P2PeerHub`, carrying a single map:

```cpp
BEGIN_P2PeerMsg_MAP(PipeMapHub, P2PeerHub)
    ON_P2PeerMsg(kMsgPing, On_HubPing)   // server intercepts the request
    ON_P2PeerMsg(kMsgPong, On_HubPong)   // client intercepts the reply
END_P2PeerMsg_MAP()
```

Delivery is **by destination address**, so only the relevant handler fires per role:

- The **client** posts `HubPing` (`src=Client`, `dst=Server`). The framework routes it
  across the pipe; on arrival the server's map matches `ON_P2PeerMsg("HubPing", …)`.
- Inside `On_HubPing` the server **generates an additional `P2PeerMsg`** — a `HubPong`
  addressed back to `pMsg->GetSource()` — and posts it for subsequent routing.
- Back on the client, the map matches `ON_P2PeerMsg("HubPong", …)` and the round trip
  completes. (Each end's *other* handler is simply never routed to.)

Message names are wide strings (`P2PmsgID == LPCWSTR`); the same literal is used both in
the map entry and in the `P2PeerMsg32(src, dst, name, data, bytes)` constructor.

The declaration side is `DECLARE_P2PeerMsg_MAP()` inside the class body.

## `P2PeerMsg_MAP` — the framework contract

The `P2PeerMsg_MAP` is supported by any object derived from `P2PeerTarget`. Any `P2PeerMsg`
objects for which the destination address matches the hub address are pumped through the hub
`P2PeerTarget` hierarchy until a matching handler is located.

`P2PeerMsg`'s routed to a hub are subsequently pumped through the registered `P2PeerTarget`
hierarchy until intercepted by a matching handler. An optional network exception is thrown for
unhandled `P2PeerMsg`'s.

Intercepted `P2PeerMsg`'s may be optionally reflected, acknowledged, generate a network
exception or additional `P2PeerMsg`'s for subsequent routing and processing.

Pumped `P2PeerMsg`'s can be intercepted by one of the following macros according to state.
Wildcards can be used to collectively express `strMsgName`'s.

```cpp
BEGIN_P2PeerMsg_MAP (class, base)
  // Normal state message interceptions
  ON_P2PeerMsg ( strMsgName, mFxn )
  // Network message exception catch
  ON_P2PeerMsg_CATCH ( strMsgName, mFxn )
  // Reflected message interception
  ON_P2PeerMsg_REFLECT ( strMsgName, mFxn )
  // Reflected message exception catch
  ON_P2PeerMsg_REFLECT_CATCH ( strMsgName, mFxn )
  // Acknowledged message interception
  ON_P2PeerMsg_ACK ( strMsgName, mFxn )
  // Acknowledged message exception catch
  ON_P2PeerMsg_ACK_CATCH ( strMsgName, mFxn )
END_P2PeerMsg_MAP ( )
```

`P2PeerMsg` objects are pumped into handlers in a fully thread-safe context with full read-write
access. Information may be both appended to or removed from the pumped message.

Load balancing can be managed within handlers via single calls to
`P2PeerTarget::P2PeerContext()` and `P2PeerContextSwap()`. In which case the `P2PeerMsg` is
immediately pumped back into the handler in the nominated context.

## Verdict = process exit code

`0` = SUCCESS (client received the server's reply) · `2` = assert · `3` = timeout ·
`1` = setup failure. A CRT/MFC assert is trapped and turned into a deterministic `exit(2)`
so a headless run never hangs on a modal dialog.

## Build & run

Mirrors `PipeMeshTest`: references the sibling `Msgcore` / `TargetCore` projects
(`..\..\..\lib` for import libs, `..\..\..\Msgcore` / `..\..\..\TargetCore` for headers).

```
msbuild "PipeMsgMapTest(2022).vcxproj" /p:Configuration=Debug /p:Platform=x64
# run: x64\Debug\PipeMsgMapTest.exe
```

> **Why no `/DELAYLOAD` here** (unlike `PipeMeshTest`): defining a `P2PeerMsg_MAP` pulls in
> the imported **data** symbol `P2PeerHub::P2PeerMsgMap`, and the linker refuses to
> delay-load a DLL from which a data symbol is imported (`LNK1194`). So `TargetCore.dll`
> is loaded normally and the post-build step copies it (from `..\..\..\bin\Debug64\`) next to
> the exe, alongside `Msgcore.dll`.

## Verified result (runtime)

Built with MSBuild (`Debug|x64`, VS2022 v143) and run headless — **exit code 0 (PASS)**:

```
[CLIENT] Posted 'HubPing' -> 'MsgMap.Server': "Ping: hello Server, this is Client over a named pipe."

[SERVER] On_HubPing (recv)  name='HubPing' from='MsgMap.Client'
  > Ping: hello Server, this is Client over a named pipe.

[SERVER] Generated reply 'HubPong' -> 'MsgMap.Client'

[CLIENT] On_HubPong (recv)  name='HubPong' from='MsgMap.Server'
  > Pong: server got your ping, replying over the pipe.

[MAIN] SUCCESS - client received the server's HubPong reply
Done (exit=0).
```

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](../../LICENSE) for
the full text.

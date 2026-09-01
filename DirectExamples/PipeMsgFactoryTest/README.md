# PipeMsgFactoryTest — factory-based sends into `ON_P2PeerMsg`

A variant of [`PipeMsgMapTest`](../PipeMsgMapTest): identical two-hub, single-process,
named-pipe (`P2PeerConPipe`) request/response round trip routed by a `P2PeerMsg_MAP` —
but the **send side** no longer hand-constructs a `P2PeerMsg32`. Instead each targeted
(named) message is produced through the framework's **factory** API and posted with the
ordinary `PostP2PeerMsg()`, proving factory-built messages route to the same
`ON_P2PeerMsg` handlers.

```
client --HubPing(RedirectFactory)--> [named pipe] --> server.On_HubPing
server --HubPong(RedirectFactory)--> [named pipe] --> client.On_HubPong   => DONE
```

The `P2PeerMsg_MAP` and both handlers are byte-for-byte the same as `PipeMsgMapTest`:

```cpp
BEGIN_P2PeerMsg_MAP(FactoryMapHub, P2PeerHub)
    ON_P2PeerMsg(kMsgPing, On_HubPing)   // server intercepts the request
    ON_P2PeerMsg(kMsgPong, On_HubPong)   // client intercepts the reply
END_P2PeerMsg_MAP()
```

Only the two origination sites changed.

## The factory used: `RedirectFactory(dst, name, data, size)`

Both ends seed a source-bearing template and let the factory produce the addressed,
routable, caller-owned message:

```cpp
// CLIENT ping (src=Client, dst=Server, name="HubPing")
P2PeerMsg32 oSeed(kClientAddr, kClientAddr, kMsgPing, lpszMsg, nBytes);
P2PeerMsg*  pMsg = oSeed.RedirectFactory(kServerAddr, kMsgPing, lpszMsg, nBytes);
PostP2PeerMsg(pMsg);

// SERVER reply (src=Server, dst=<ping sender>, name="HubPong")
P2PeerMsg32 oSeed(kServerAddr, kServerAddr, kMsgPong, lpszReply, nBytes);
P2PeerMsg*  pReply = oSeed.RedirectFactory(pMsg->GetSource(), kMsgPong, lpszReply, nBytes);
PostP2PeerMsg(pReply);
```

`RedirectFactory(dst,name,data,size)` is `new P2PeerMsg(GetSource(), dst, name, data, size)` —
a **fresh** message with no inherited routing state — which is why it routes cleanly across
the pipe. This is the same idiom the framework itself uses to forward messages between hubs
(`PostP2PeerMsg(pMsg->RedirectFactory(dst))` in `P2PeerExplorer.cpp`).

## Caveat discovered: `ResponseFactory` is **not** a standalone cross-hub reply

The obvious "reply" factory is `ResponseFactory(name, data, size)`, which reverses the
received message's src/dst. It builds correct string addressing — **but** it also copies the
received message's envelope prefix (`spMsg->r_data() = r_data()` in
`P2PeerMsg.cpp:360`), inheriting its resolved routing state. Posting that result standalone
loops the reply **back into the current hub's own map** instead of routing it across the
pipe. Observed directly: an earlier draft of this test using
`PostP2PeerMsg(pMsg->ResponseFactory(...))` delivered the pong to `[SERVER] On_HubPong`
rather than `[CLIENT] On_HubPong`, yet still tripped the done-event (green for the wrong
reason).

The framework never standalone-posts a `ResponseFactory` result — it returns it to the pump
(`P2PeerHub.cpp:1054`), which performs the routing. So in a hand-rolled handler, prefer
`RedirectFactory` (fresh addressing) for a cross-hub reply.

## Ways to send a targeted (named) message — summary

Build it, then inject it:

| Build (targeted + named) | Inject / route |
| --- | --- |
| `P2PeerMsg32/16/64(src, dst, name, data, size)` ctor | `PostP2PeerMsg(pMsg)` — primary send |
| base `P2PeerMsg(src, dst, name, …)` ctors | `RouteP2PeerMsg(pMsg)` |
| `RedirectFactory(dst)` / `RedirectFactory(dst, name, data, size)` | `RedirectP2PeerMsg(pMsg, dst)` — retarget in-flight |
| `ResponseFactory` / `WrappedResponseFactory` (reply; see caveat) | `ReflectP2PeerMsg(pMsg)` — bounce to source |
| `ReflectFactory()` / `LoopbackFactory()` (swap src↔dst) | `RepumpP2PeerMsg` / `ReturnP2PeerMsg` |
| copy ctor `new P2PeerMsg(*pMsg)` | `PostPITimer(pMsg, …)` — deferred |

## Verdict = process exit code

`0` = SUCCESS (client received the reply) · `2` = assert · `3` = timeout · `1` = setup.

## Build & run

Mirrors `PipeMsgMapTest` (same `..\..\..\lib` import libs, `..\..\..\Msgcore` / `..\..\..\TargetCore`
headers, no `/DELAYLOAD` because `BEGIN_P2PeerMsg_MAP` imports a data symbol).

```
msbuild "PipeMsgFactoryTest(2022).vcxproj" /p:Configuration=Debug /p:Platform=x64
# run: x64\Debug\PipeMsgFactoryTest.exe
```

## Verified result (runtime)

Built with MSBuild (`Debug|x64`, VS2022 v143) and run headless — **exit code 0 (PASS)**:

```
[CLIENT] Posted 'HubPing' -> 'MsgFac.Server' (RedirectFactory): "Ping: hello Server, this is Client via RedirectFactory."

[SERVER] On_HubPing (recv)  name='HubPing' from='MsgFac.Client'
  > Ping: hello Server, this is Client via RedirectFactory.

[SERVER] RedirectFactory reply src='MsgFac.Server' dst='MsgFac.Client'
[SERVER] Posted 'HubPong' -> 'MsgFac.Client'

[CLIENT] On_HubPong (recv)  name='HubPong' from='MsgFac.Server'
  > Pong: server got your ping, replying via RedirectFactory.

[MAIN] SUCCESS - client received the server's HubPong reply
Done (exit=0).
```

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](../../LICENSE) for
the full text.

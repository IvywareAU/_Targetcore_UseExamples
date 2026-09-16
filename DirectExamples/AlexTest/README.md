# AlexTest — P2P Messaging Demo

A minimal Visual Studio 2026 console application that demonstrates peer-to-peer TCP/IP messaging using the **Targetcore** kernel library.

---

## What It Does

Two instances of the same executable talk to each other over TCP/IP:

- **Server instance** — listens on port 7777, prints every message it receives.
- **Client instance** — connects to the server, sends one text message, then exits.

No configuration files or shared state required. Start the server first, then run the client.

---

## Building

Open the tree-wide **`..\DirectExamples(2026).sln`** in Visual Studio 2026, select the
**AlexTest** project, and build **Debug | x64** or **Release | x64**.
Both configurations are supported and produce a runnable exe:

| Configuration | Output |
|---|---|
| `Debug \| x64` | `.\x64\Debug\AlexTest.exe` |
| `Release \| x64` | `.\x64\Release\AlexTest.exe` |

The required DLLs are copied next to the exe by the post-build step, so no PATH changes
are needed.

**Prerequisites** (environment variables must be set):

| Variable | Value | Purpose |
|---|---|---|
| `WDMSCS_LIB` | `<MSCS>\lib` | Import library root (`$(Platform)` subfolder is used) |
| `WDMSCS_DEBUG` | `<MSCS>\bin\Debug64` | Where the Debug kernel DLLs are deployed |

Where the post-build step picks up each kernel DLL:

| Configuration | `Msgcore` | `Targetcore` |
|---|---|---|
| Debug | `..\..\..\Msgcore\x64\Debug\` | `$(WDMSCS_DEBUG)` |
| Release | `..\..\..\Msgcore\x64\Release\` | `..\..\..\Targetcore\x64\Release\` |

**Build `Msgcore` and `Targetcore` first.** If the `Targetcore` DLL is missing the
post-build step fails the build, rather than producing an exe that dies at startup
with `0xC06D007E` (delay-load: module not found).

The project links against (from `..\..\..\lib\$(Platform)`):
- `Msgcore.lib` — P2P message data model
- `Targetcore.lib` — P2P networking kernel (TCP/IP transport)

---

## Running

Open **two separate terminal windows**, both in `.\x64\Debug`.

### Terminal 1 — Server (receiver)

```
AlexTest.exe
```

Output:
```
=== AlexTest — P2P Messaging Demo ===
Mode  : SERVER  (listen on port 7777)

Hub 'AlexTest.Server' started.
Listening on port 7777 — press Enter to exit.
```

The server blocks until you press **Enter**. Leave it running and switch to terminal 2.

---

### Terminal 2 — Client (sender)

```
AlexTest.exe send 127.0.0.1 "Hello from client!"
```

Arguments:

| Position | Value | Default |
|---|---|---|
| 1 | `send` | *(omit for server mode)* |
| 2 | Target IP address | `127.0.0.1` |
| 3 | Message text | `Hello from AlexTest client!` |

Output:
```
=== AlexTest — P2P Messaging Demo ===
Mode  : CLIENT  (send one message)
Target: 127.0.0.1:7777

Hub 'AlexTest.Client' started.
Connecting to 127.0.0.1:7777 ...
[CLIENT] Login ack from 'AlexTest.Server' — connection ready.
[CLIENT] Posted: "Hello from client!"
[CLIENT] Message delivered.
Shutting down...
Done.
```

Back in terminal 1, the server prints:

```
[SERVER] BCast from 'AlexTest.Client':
  > Hello from client!
```

Press **Enter** in terminal 1 to shut the server down.

---

## How It Works

### Library Stack

```
AlexTest.exe
    │
    ├── Msgcore.dll        P2P message data model
    │     P3PmsgField, P3PmsgList, P2PmsgMgr ...
    │
    └── Targetcore.dll     P2P networking kernel
          P2PeerHub, P2PeerConWsa, P2PeerMsg ...
```

### Key Classes

| Class | From | Role |
|---|---|---|
| `P2PeerHub` | Targetcore | Message router. Owns the IOCP pump thread and all connections. |
| `P2PeerConWsa` | Targetcore | Single TCP/IP connection (WSA + IOCP). |
| `P2PeerMsg` | Targetcore | Typed, self-describing message packet exchanged between hubs. |
| `AlexTestHub` | AlexTest | Our subclass of `P2PeerHub`. Overrides virtual handlers. |

### Addressing

Every hub has a **virtual network address** — a dot-separated name string:

```
"AlexTest.Server"   ← server hub
"AlexTest.Client"   ← client hub
```

These addresses are exchanged during the login handshake and used by the routing engine to decide which TCP connection to send a message through.

### Message Flow

```
CLIENT                                          SERVER
  │                                               │
  │  AlexTestHub("AlexTest.Client")               │  AlexTestHub("AlexTest.Server")
  │  SpawnHub()  ──── hub thread starts ──────────│  SpawnHub()
  │                                               │
  │  P2PeerConWsa::ClientFactory(...)             │  P2PeerConWsa::ServiceFactory(...)
  │  PostP2PeerCon(pCon)                          │  PostP2PeerCon(pCon)
  │                                               │
  │  ── TCP connect ───────────────────────────►  │
  │  ◄── P2PmsgLogin handshake ────────────────►  │  (addresses exchanged)
  │                                               │
  │  On_ConLoginAck() ← fires on client           │
  │    PostTestMessage()                          │
  │       new P2PeerMsg32(src, dst, BCast, data)  │
  │       PostP2PeerMsg(pMsg)                     │
  │                                               │
  │  ── P2PeerMsg over TCP ────────────────────►  │
  │                                               │  On_P2PeerBCast(pMsg)
  │                                               │    wprintf("Received: ...")
  │                                               │
  │  Sleep(1s), CloseHub()                        │  getchar(), CloseHub()
```

### AlexTestHub — Virtual Handler Overrides

`AlexTestHub` derives from `P2PeerHub` and overrides three virtual methods. No custom message-map macros are needed — the base class map already wires these up:

```cpp
// Server: print any arriving broadcast message
virtual msgRESULT On_P2PeerBCast(P2PeerMsg* pMsg) override;

// Server: print any arriving unicast message
virtual msgRESULT On_P2PeerUCast(P2PeerMsg* pMsg) override;

// Client: called once TCP+login handshake is complete — send the message now
virtual conRESULT On_ConLoginAck(P2PeerCon*, P2PaddrSTR, P2PaddrSTR,
                                  const void*, P2Psize_t) override;
```

### MFC Extension DLL Note

`Targetcore.dll` is an **MFC Extension DLL**. Its `DllMain` calls `AfxInitExtensionModule` / `new CDynLinkLibrary(...)`, which requires MFC's thread state (`AfxGetThread()`) to be valid. That state is set up by the `CWinApp theApp` global — which is constructed *after* implicit DLLs would normally load.

To avoid the timing conflict, the project links `Targetcore` with `/DELAYLOAD`, so the DLL is loaded on the first actual API call (inside `main()`, after `CWinApp` is fully constructed).

---

## Project Structure

```
AlexTest\
  AlexTest(2026).vcxproj      Project: Console | MFC Dynamic | Unicode | x64
  AlexTest(2026).vcxproj.filters
  Targetver.h                 Windows 10 SDK target
  stdafx.h / stdafx.cpp       Precompiled header (MFC + WinSock2)
  AlexTest.h                  Forward declarations
  AlexTest.cpp                AlexTestHub class + main()
  README.md                   This file
```

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](../../LICENSE) for
the full text.

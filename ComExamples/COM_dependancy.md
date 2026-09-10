# Where the COM interface dependency lives

Across all eleven COM harnesses in this tree, the dependency on TargetCom is
**not** in any harness project. Every `<Harness>\<Harness>Com(2026).vcxproj`
contains nothing but its one `.cpp` and an import of the shared property sheet:

```xml
<!-- AlexTest\AlexTestCom(2026).vcxproj:37 -->
<Import Project="..\common\Com.props" />
```

The dependency lives in exactly three places, and each is a different kind of
binding.

## 1. Compile time — the MIDL output, via `common\Com.props`

| file:line | what |
|---|---|
| `common\Com.props:32` | `<ComIdlDir>$(FacadeRoot)out\$(Platform)\$(Configuration)\obj\TargetCom\</ComIdlDir>` — resolves to `..\..\..\TargetFacade\out\<Platform>\<Config>\obj\TargetCom\`, the TargetCom project's *intermediate* directory |
| `common\Com.props:61` | that directory goes on `AdditionalIncludeDirectories`, together with `common\` itself |

That directory holds the MIDL output compiled from
`..\..\..\TargetFacade\com\TargetCom.idl`:

```
TargetCom_h.h     interface declarations
TargetCom_i.c     GUID definitions
TargetCom_p.c     proxy/stub
TargetCom.tlb     the type library
```

## 2. Where the harnesses consume it — `common\ComHarness.h`

| file:line | what |
|---|---|
| `common\ComHarness.h:48` | `#include "TargetCom_h.h"` — `IP2PNetworkCom`, `IP2PHubCom`, `_IP2PHubEvents` |
| `common\ComHarness.h:54-56` | `#include "TargetCom_i.c"` for `CLSID_P2PNetwork`, `IID_IP2PHubCom`, `DIID__IP2PHubEvents`, guarded by `COMHARNESS_NO_GUIDS` |

Both of those are **generated**, which is what makes the ABI-4 reissue of
`IP2PHubCom` (new IID `{12D65CF2-…}`) a non-event at compile time and a hard
failure at run time: rebuilding regenerates `TargetCom_i.c` with the new value,
so a harness rebuilt here and a server registered from an older build simply do
not meet — `CreateHub` returns the hub, `QueryInterface` refuses it. That is the
intended outcome, and the reason the interface got a new identity rather than a
quietly changed shape.

No harness `.cpp` includes a COM header directly. They all include
`ComHarness.h` and nothing else from the layer.

## 3. Run time — the registry, not the linker

| file:line | what |
|---|---|
| `common\Com.props:57` | the only libraries are `ole32.lib;oleaut32.lib;uuid.lib`. No `TargetFacade.lib`, no `TargetCore.lib`, no project `.lib` at all |
| `common\ComHarness.h:518` | `CoCreateInstance(CLSID_P2PNetwork, NULL, CLSCTX_INPROC_SERVER, IID_IP2PNetworkCom, ...)` — the implementation is found in `HKCU\Software\Classes` |
| `run_all.ps1:50-52` | registers the **staged** `bin\<Config>\TargetCom(2026)[d].dll` with `regsvr32 /s /n /i:user` |
| `run_all.ps1:104` | unregisters it again |
| `script\ps_client.ps1:46` | `New-Object -ComObject TargetCom.P2PNetwork` — ProgID only |
| `script\vbs_client.vbs:46` | `CreateObject("TargetCom.P2PNetwork")` — ProgID only |
| `common\Com.props:59-65` | post-build step stages the four runtime DLLs (TargetCom, TargetFacade, TargetCore, Msgcore) next to the exes |

## Summary

```
compile   ->  TargetCom_h.h + TargetCom_i.c   (include path, Com.props:28+52)
link      ->  ole32 / oleaut32 / uuid          (Com.props:57)
run       ->  HKCU\Software\Classes            (CoCreateInstance / ProgID)
```

## The implicit dependency worth flagging

`ComExamples(2026).sln` lists only the eleven harness projects. There is
**no `ProjectReference` to TargetCom.** The include path at `Com.props:28`
points into another tree's build output, so if `TargetFacade\com` has not been
built for that exact configuration, the harnesses fail at
`#include "TargetCom_h.h"` with nothing in the error to explain the ordering.

`Com.props:10-12` documents the requirement ("TargetCom must be built before
these are. Building the solution does that."), but nothing inside *this*
solution enforces it.

---

# From what languages these COM interfaces can be called

Everything about the interface shape in `..\..\..\TargetFacade\com\TargetCom.idl`
is chosen to maximise the caller list:

* both interfaces are `dual` — vtable **and** `IDispatch` (`TargetCom.idl:43`, `:87`)
* every parameter is automation-compatible: `BSTR`, `VARIANT`, `VARIANT_BOOL`,
  `LONG` — never `unsigned short` or a raw pointer (`TargetCom.idl:9-12`).
  Since ABI 4 there are **no numeric parameters left on `IP2PHubCom` at all**:
  a port, a pipe name, a Dmx service and a COM port all arrive inside the
  endpoint `BSTR`, so the widest-possible caller list now needs exactly one
  automation type to reach every transport
* there is a registered type library, `EACCF9B6-48FF-4F4B-9B10-CC5D3046639C`
  (`TargetCom.idl:105`)
* `P2PNetwork.rgs` registers `ThreadingModel = Both` plus the ProgID
  `TargetCom.P2PNetwork`

## Tier 1 — late-bound, no tooling at all (`IDispatch`)

Anything that can `CreateObject("TargetCom.P2PNetwork")`:

* **VBScript / JScript** under `cscript` / `wscript`, WSF, logon scripts
  — proven by `script\vbs_client.vbs:46`
* **PowerShell** (`New-Object -ComObject`) — proven by `script\ps_client.ps1:46`
* **VBA**: Excel, Word, Outlook, Access macros
* **Classic ASP** (`Server.CreateObject`)
* **Python** (`win32com.client.Dispatch`, comtypes), **Perl** (`Win32::OLE`),
  **Ruby** (`WIN32OLE`), **AutoIt**, **LabVIEW**, **MATLAB** (`actxserver`)

`vbs_client.vbs` is the floor of this tier — no byte arrays, no structs, no
HRESULT inspection beyond `Err.Number` — and it passes. `Send` takes a `VARIANT`
rather than a `SAFEARRAY` (`TargetCom.idl:64`) precisely so a scripting host can
hand it a string; `PipeMsgFactoryTest` demonstrates C++ passing a struct as
bytes and VBScript passing a string into the same `Send`.

## Tier 2 — early-bound, from the type library

* **C# / VB.NET / F#** via a COM reference or `TlbImp`; `dynamic` also works
  without one
* **VB6 / VBA with a project reference** (IntelliSense, named constants)
* **Delphi**, **C++Builder** via type-library import
* **Rust** (`windows-rs`), **Go** (`go-ole`), **Java** (Jacob, com4j)

This tier additionally gets the `P2PFError` enum
(`p2pfConDuplicate = 0x80040204`, …, `TargetCom.idl:115-133`) as named
constants. `TwoConTest` exists to show that the duplicate-peer rejection crosses
out to automation unchanged, so a caller can branch on it.

The enum grew with ABI 4 and now ends on one that is **not** an error:

```
p2pfEndpoint      = 0x80040208   endpoint string unparseable or unsupported
p2pfUnresolved    = 0x80040209   endpoint omitted and nothing to resolve it to
p2pfNoHub         = 0x8004020A
p2pfLinkPartial   = 0x8004020B
p2pfUnrelatedLink = 0x0004020C   <- severity 0: ARMED, but the two addresses
                                    are neither ancestor nor descendant
```

That leading `0` matters to an early-bound caller: .NET and VB6 raise on a
failed HRESULT and stay silent on a successful one, so `p2pfUnrelatedLink`
arrives as *no exception at all*. A caller that wants to see it has to check the
returned value rather than wait to be thrown at — and a caller that ignores it
still gets a working link, which is the point of making it a success code.

## Tier 3 — raw vtable

* **C / C++** through `TargetCom_h.h` + `TargetCom_i.c` — what all eleven
  harnesses in this tree do, using nothing but plain COM: no ATL, no
  `_com_ptr_t`, no `#import`, no MFC.

## Two constraints that actually bite

**Bitness.** An in-proc server can only be loaded by a caller of its own
bitness, so both are now built. `TargetCom(2026).vcxproj` and
`TargetFacade(2026).vcxproj` each carry four configurations —
`Debug|x64`, `Release|x64`, `Debug|Win32`, `Release|Win32` — and
`TargetFacade(2026).sln` exposes all four (the two smoke-test projects stay
x64-only and are skipped in a `Win32` solution build).

```
msbuild "TargetFacade(2026).sln" -p:Configuration=Debug -p:Platform=x64     # 64-bit callers
msbuild "TargetFacade(2026).sln" -p:Configuration=Debug -p:Platform=Win32   # 32-bit callers
```

**Build the kernel first, and mind the platform NAME.** The facade solution
calls 32-bit `Win32`; `TargetCore(2026).sln` calls it **`x86`** (mapping to the
project's `Win32`). Passing `-p:Platform=Win32` to the kernel solution fails
with MSB4126 and reads like "there is no 32-bit build", which is not what it
means:

```
msbuild "TargetCore(2026).sln" -p:Configuration=Debug -p:Platform=x86        # NOT Win32
```

The kernel has to go first because it is what produces
`lib\Win32\TargetCore(2026)*.lib`. Without it the facade fails at
`LNK1104: cannot open file 'TargetCore.lib'` — and only on a *clean*
build, because an incremental one happily reuses the previous link. If the
32-bit chain looks fine, confirm it with `-t:Rebuild` before believing it.

The 32-bit build differs from the 64-bit one in exactly two settings: MIDL runs
with `/env win32`, and the facade links the kernel import libraries from
`..\..\lib\Win32` instead of `..\..\lib`. Output goes to
`TargetFacade\out\Win32\<Config>\` (x64 to `out\x64\<Config>\`), so the two
never collide. `$(WDMSCS_LIB)` is redirected per-platform by
`Directory.Build.props:4`, so `lib\Win32` and `lib\x64` are populated
automatically and same-named import libraries never clobber one another.

Register each with the matching `regsvr32`, and run each next to its own three
dependency DLLs — an x86 `TargetCom` needs the x86 `TargetFacade`,
`TargetCore` and `Msgcore`:

```
%SystemRoot%\System32\regsvr32.exe  /n /i:user  ...\com\x64\Debug\TargetCom.dll
%SystemRoot%\SysWOW64\regsvr32.exe  /n /i:user  ...\com\Win32\Debug\TargetCom.dll
```

The two registrations are independent — 64-bit under
`HKCU\Software\Classes\CLSID`, 32-bit under
`HKCU\Software\Classes\WOW6432Node\CLSID` — so both can be present at once and
each caller silently gets the one it can load.

Verified, **re-verified against ABI 4**, and re-verified again after the 32-bit
chain was repaired: `script\vbs_client.vbs` driven by **32-bit**
`SysWOW64\cscript.exe` against the Win32 server passes 11/11 checks, exit 0, on
both `Debug|Win32` and `Release|Win32` — real Dmx traffic over a `dmx://`
endpoint, plus the reserved-topic, bad-endpoint and closed-hub HRESULTs. That
covers VB6, 32-bit Office/VBA, 32-bit WSH and classic ASP under a 32-bit IIS
worker.

**The 32-bit chain was broken for a while, and silently.** `P2PeerHub.cpp:74`
uses `std::atomic_ref` (C++20) while the kernel's `Win32` configurations were
still on `stdcpp17` — the `x64` ones had been moved to `stdcpplatest` and the
`Win32` ones had not. So the 32-bit kernel would not compile, no x86
`TargetCore` import library or DLL existed anywhere in the tree, and the facade
could not link 32-bit from clean. What hid it: the stale x86 `TargetFacade` /
`TargetCom` DLLs left in `Win32\<Config>\` from before the rot made incremental
builds appear to succeed, and nothing in the routine test matrix is 32-bit. Two
lessons worth keeping — **build the 32-bit chain with `-t:Rebuild`**, and treat
any compiler-setting change to one platform as owing the same change to the
other.

One practical note from doing it: `com\Win32\<Config>\` is an *intermediate*
directory with no DLLs staged into it, so `regsvr32` there fails with exit 3
(`LoadLibrary` cannot resolve `TargetFacade`, `TargetCore`, `Msgcore`). Copy the
four next to each other first — which is exactly what `Com.props`' staging step
does for x64, and what `bin\<Config>` is for.

Still x64-only: the eleven C++ harnesses in this tree and `run_all.ps1`.
`common\Com.props:28` hard-codes `com\x64\$(Configuration)` for the MIDL
include path, and its staging step copies the x64 DLLs. The harnesses are
64-bit clients of a 64-bit server; nothing about the Win32 server needs them.

**Events are a narrower list than calls.** The connection point
`_IP2PHubEvents` — `OnMessage`, `OnPeerUp`, `OnPeerDown`, `OnError`
(`TargetCom.idl:140-156`) — is sinkable from C++ (`common\ComHarness.h:408-423`),
from VB6, and from .NET with an interop assembly. It is **not** sinkable from
PowerShell or WSH:

* .NET, and therefore PowerShell, needs a generated interop assembly for the
  coclass to build the equivalent of `ComHarness.h`'s hand-written `EventSink`
* WSH can only sink events on objects it created itself via
  `WScript.CreateObject(progid, prefix)` — but hubs come from `CreateHub`, and
  the `P2PHub` coclass is marked `noncreatable` (`TargetCom.idl:169`)

Scripts observe traffic through `IsPeerUp` and `Broadcast`'s return value
instead. Both are real proof that bytes moved.

**Every caller in an STA must pump messages.** Hub events are raised on a
dispatch thread inside the DLL and marshalled into the caller's apartment
through the message queue, so a plain `WaitForSingleObject` hangs forever.
`com::Gate::wait()` (`common\ComHarness.h:201-220`) is what a C++ caller needs;
script hosts and Office pump for you.

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](../LICENSE) for
the full text.

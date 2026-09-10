# PanamaJavaExamples

Every harness in [`DirectExamples`](../DirectExamples), rewritten a **fifth**
time — in **Java**, on the same [TargetFacade](../../TargetFacade) that
[`FacadeExamples`](../FacadeExamples) drives from C++.

Same questions, same verdicts, same exit-code contract as the other four trees,
so all five read side by side.

```
.\build.ps1                          # javac, x64, JDK 22+
.\run_all.ps1                        # build + run everything, summarise
.\run_all.ps1 -Config Release
```

**Status: 14/14 on Debug|x64 and 14/14 on Release|x64** (12 harnesses; the two
two-process ones count their server and client separately). This is the only tree
of the five that runs *all twelve* originals — see
[`ExplorerTest`](#explorertest-the-one-the-light-tree-skipped) below.

`RouteLoopbackTest` briefly regressed against a rebuilt kernel and is green
again — see [downward relay](#downward-relay-and-security_review-m2) below.

There is a caveat on the Release number, and it is not new: see
[the teardown fault](#the-release-teardown-fault-is-not-a-java-problem-either).

## What makes this pass different: no build-time relationship at all

| | links / references | binds to the implementation |
|---|---|---|
| `DirectExamples` | `TargetCore.lib` + `Msgcore.lib`, MFC | at link time |
| `FacadeExamples` | `TargetFacade.lib` | at link time |
| `ComExamples` | `ole32`/`oleaut32`/`uuid` + the type library | registry, at run time |
| `dotNetExamples` | `mscorlib` / `System` / `System.Core` | registry, at run time |
| **`PanamaJavaExamples`** | **nothing at all** | **`LoadLibrary`, at run time** |

`javac` is handed no MSCS input of any kind — not a header, not a `.lib`, not a
`.tlb`, not a NuGet package, not a jextract-generated binding. The DLL is
located and loaded at run time by path, and every call into it is assembled from
`java.lang.foreign` primitives.

The two COM trees make the same claim about *build-time* independence, but they
get theirs from the registry: `regsvr32` has to have run, and a `CoCreateInstance`
resolves a GUID through `HKCU\Software\Classes`. This tree needs no registration,
no elevation and no COM at all — copy three DLLs next to the classes and run.

That is possible because of a property the facade's own header states and no
previous tree had to lean on:

> Pure-vtable interfaces + one `extern "C"` factory. No C++ classes are exported,
> so the facade is usable from any MSVC toolset (**and any language that can call
> a vtable**) without name-mangling / CRT coupling.
> — [`TargetFacade.h:12-16`](../../TargetFacade/include/TargetFacade.h)

Java is that language, and this tree is the claim taken literally. There is no
JNI, no `jextract`, and **no native code of ours anywhere** — the only C++ in the
picture is the facade that already shipped.

## How a call works

An interface pointer points at an object whose first 8 bytes are a pointer to its
vtable; the vtable is an array of function pointers in declaration order. So
`hub->Listen(a, b)` is: read the vptr, read slot 0, downcall to it with `this`
first. That is the whole of [`Native.slot`](src/mscs/p2pf/Native.java):

```java
public static MemorySegment slot(MemorySegment obj, int index) {
    MemorySegment vptr = obj.reinterpret(PTR).get(ADDRESS, 0);
    return vptr.reinterpret((index + 1L) * PTR).getAtIndex(ADDRESS, index);
}
```

The sink runs the same machinery backwards. `IP2PHubEvents` is four methods and —
crucially — **not** derived from `IUnknown`: no `QueryInterface`, no `AddRef`, no
`Release`. So implementing it is exactly four function pointers in a row plus a
pointer to that row, which [`Hub`](src/mscs/p2pf/Hub.java) builds out of four
Panama upcall stubs bound to its own instance methods. The facade cannot tell it
from one a C++ compiler emitted.

| the tree needs | Java spelling |
|---|---|
| call a method on `IP2PHub` | `Linker.downcallHandle(fd)` on `slot(hub, n)` |
| implement `IP2PHubEvents` | four `Linker.upcallStub`s in a vtable Java allocates |
| a `const wchar_t*` argument | `arena.allocateFrom(s, UTF_16LE)` |
| a `const wchar_t*` return | `p.reinterpret(MAX).getString(0, UTF_16LE)` |
| `HRESULT` | `int`, and `FAILED()` is `hr < 0` |

### The one piece of luck

Every string on this ABI is a NUL-terminated `wchar_t*`, which on Windows is
UTF-16 — `java.lang.String`'s own encoding. The conversion is a copy, not a
transcode, and a Java consumer of MSCS pays nothing for the kernel's Unicode
build. (The `_u8` surface `TargetCore_c.h` publishes for portability exists
because `wchar_t` is UTF-32 on Linux; here it would be pure overhead.)

### The one thing nothing checks

Slot indices. A wrong index calls a *different method with different arguments*
rather than failing to compile — the same hazard `dotNetExamples` records
for its hand-written COM interop, except that tree at least had `DispId`
attributes to get wrong in one place. Every index lives in
[`Abi.java`](src/mscs/p2pf/Abi.java) beside the signature it belongs to, and
`Network.open()` calls `VersionString` (slot 2) for its known answer before
anything else depends on the transcription being right.

This is also why `ABI_VERSION` is passed to `P2PF_CreateNetwork`: ABI 4 was a
hard cut that moved every slot after the arming pair, so a stale binding gets
`P2PF_E_ABI_MISMATCH` instead of a vtable skew. Everything since has been
append-only, which is what makes a transcribed table viable at all.

## The map

| Original | Java | What the Java version does differently |
|---|---|---|
| `WsaMeshTest` | ✅ same | `listen` / `connect` over `tcp://` |
| `PipeMeshTest` | ✅ same | the same two verbs, one string changed to `pipe://` |
| `DmxMeshTest` | ✅ same | …and to `dmx://` |
| `Com232MeshTest` | ✅ same | …and to `serial://COMn` |
| `PipeMsgMapTest` | ✅ same | `onTopic(name, λ)` replaces `BEGIN_P2PeerMsg_MAP` / `ON_P2PeerMsg` |
| `PipeMsgFactoryTest` | ⚠️ behaviour kept, mechanism gone | the facade has no factory family; one `send()` replaces `RedirectFactory` |
| `TwoConTest` | ✅ same | the duplicate-peer rule is a named error, `P2PF_E_CON_DUPLICATE` |
| `LocalInMemoryTest` | ⚠️ result kept, mechanism gone | no `PostP2Pmsg` pump injection in the facade; uses Dmx instead |
| `AlexTest` | ✅ same, minus one leg | server no longer dies on redirected stdin; no Enter-to-stop (see below) |
| `AlexInterop` | ⚠️ **loses its whole point, twice** | the original exists to be **Linux-portable** |
| `RouteLoopbackTest` | ⚠️ different framework | the original is **not an MSCS harness at all** |
| `ExplorerTest` | ⚠️ **question kept, mechanism absent** | the facade has no Explorer surface |

The first eight rows carry over from the Light tree unchanged, and its README
explains four of them at length. Three are worth adding to here.

### `AlexInterop` — the Java tree is the *least* portable of the five

The original avoids every Win32-ism so the same source compiles against the
io_uring shim with g++; it is the Linux port's Phase-3 exit criterion. The Light
rewrite forfeited that once, because TargetFacade is a Windows MFC DLL. **This
one forfeits it twice**: the binding in `mscs.p2pf` is x64-Windows-only *by
construction* — it assumes 8-byte pointers, MSVC vtable layout, and
`wchar_t == UTF-16`.

That inverts the usual reason to bind a native library from Java. Java itself
would have carried this harness anywhere; what pins it is the DLL underneath and
the ABI assumptions above it. If you are working the Linux port, use the original.

### `AlexTest` — one behaviour the C++ trees have and this one does not

The Light server waits on the message *and* on the stdin `HANDLE`, but only when
`GetConsoleMode` says stdin is a real console — that guard is the whole fix for
the original's unattended-run trap. Java has no portable equivalent:
`System.console()` returns null under a redirected stdin, which is the same
signal, but you cannot then *wait on the handle* alongside the gate.

So this version drops the Enter-to-stop leg and waits on the timeout alone. An
unattended run behaves identically; an attended one just waits. It is a smaller
behaviour, and stating it is better than faking it.

### `ExplorerTest` — the one the Light tree skipped

The Light tree has eleven harnesses. The original has **twelve**: `ExplorerTest`,
at 502 lines the largest of them, is absent from every previous rewrite. It
stands up a `P2PeerExplorer` with `P2PeerExpump_ACTIVATE`, has an *anonymous*
client log in and be assigned a slot address out of `CEX.XC*`, and reads the
answer out of a `P3PmsgItem` tree hanging off the message's data node.

None of that has a facade spelling, and the gap is not small:

1. **The facade has no Explorer surface at all** — grep `TargetFacade.h` for
   `expump` or `explorer` and there are no hits.
2. **The answer travels where the facade cannot look.** A facade `Message` is
   bytes plus (ABI 8) named fields; the Explorer answers on `r_datn()`, the
   Msgcore object model one layer below, which the facade deliberately does not
   re-export.
3. **The client must be anonymous and receive its address from the server.**
   Every facade hub is created with an address it keeps.

`IP2PHub::GetNative(void**)` does hand back the underlying `P2PeerHub*` — the
documented escape hatch — but a raw C++ object pointer is worth nothing from
Java: reaching `P2PeerExpump_ACTIVATE` through it means calling a mangled C++
free function and walking a class this binding does not transcribe. That is the
point at which "no new native code" stops being possible, and this tree's premise
is that it does not stop.

So [`ExplorerTest.java`](src/mscs/examples/ExplorerTest.java) does what the Light
tree's `RouteLoopbackTest` does with *its* untranslatable original: it asks the
same question — *what is this hub?* — using the facade's own answer to it, the
ABI 6 read side (`GetConCount` / `GetCon` / `GetEndpoint` / `Describe`). The two
answers are not the same shape, and the difference is the interesting part:

| `P2PeerExplorer` | the facade read side |
|---|---|
| a **remote** peer asks | the **local** process asks |
| over a real message | over a method call, no traffic |
| answers a `P3PmsgItem` tree: machine, executable, hub registry | answers strings and a flag word |
| needs an anonymous login and a slot assignment | needs nothing |

What the facade gives up is **remoteness** — nothing crosses a wire, so it cannot
answer "what is that *other* machine running". What it gains is the **topology
relation**, which the Explorer never reported. `P2PF_REL_*` names whether each
peer is this hub's ancestor, descendant or neither, and a wrongly-shaped link is
precisely the bug that connects, logs in and looks healthy:

```
[1] GetConCount / GetCon -- the peers this hub knows
    GetConCount -> 2
    [0] CEX.Child	dmx://P2PexpProbeJava	listen,up,descendant
    [1] Other.Peer	tcp://:7834	listen,unrelated
[3] P2PF_CON_UP -- armed is not the same as up
    => PASS: the dialled child is up, the never-dialled peer is not
```

## What the rewrite cost, in lines

Non-comment, non-blank lines, counted the same way the Light README counts them
(and reproducing its two figures exactly, which is the check that the method
matches).

| | original | light | java |
|---|---:|---:|---:|
| the eleven shared harnesses | **2376** | **930** | **833** |
| shared scaffolding | — | 106 | 84 |
| **binding layer** | — | **0** | **595** |
| total | 2376 | 1036 | 1512 |

The harnesses themselves come out slightly shorter than the C++ facade versions.
The tree as a whole does not, and that is the honest accounting: **Light gets its
binding for free by `#include`-ing a header, and this tree has to write one.**
595 lines is what "no build-time relationship with MSCS" actually costs, paid
once.

Worth noting where those 595 lines are *not*: there is no per-transport code
(all four go through the same two methods, because the transport is a string),
and no per-message-type code. The binding grew for none of the twelve harnesses
after the first.

## Findings

### Downward relay and SECURITY_REVIEW M2

For a window, `RouteLoopbackTest` failed (exit 3) in **all four** example trees,
in both configurations, on the same scenario: **[2]** `Root → Leaf`, straight
down the tree through A. The receiving end said why:

```
[ERROR] []P2PeerCon::GateAppMsgInbound(this=)
Message source [RouteMesh.Root] is not the logged-in identity [RouteMesh.Root.A]
ADVICE  : Connection dropped out
```

`P2PeerCon::GateAppMsgInbound`
(`P2PeerCon.cpp:2015`) is the SECURITY_REVIEW **M2 source binding**: the source
address is read off the wire, so without it a peer that logged in as one identity
could attribute its messages to another — and the source is what routing, the
Explorer registry and every application handler key on. The rule is that a
message's source must be the peer's own address **or a descendant of it**
(`IsRable()` — exact match or a hop-boundary prefix).

The effect was **asymmetric**, which is why only one of the four scenarios
failed:

| leg | receiving hub's peer | source | verdict |
|---|---|---|---|
| **[1]** `Leaf → B`, up then down | B's peer is `Root` | `Root.A.Leaf` | passed — below Root |
| **[2]** `Root → Leaf`, down via A | Leaf's peer is `Root.A` | `Root` | **refused** — above A |

The same rule cost scenario **[4]** its grandchild — a root broadcast reached A
and B but not Leaf — though that check only asserts the direct children, so [4]
still passed.

It was never caused by the flat-C removal: verified by rebuilding the kernel
**with** `TargetCore_c.*` restored and re-running, where the failure was
identical.

**Fixed** (`P2PeerCon.cpp`, 2026-08-13): an **ancestor link is admitted without a
source check**. Links to a descendant and to an unrelated peer are completely
unchanged — those are the forgery cases the rule was written for. What the fix
grants is narrow: an ancestor may now also present a source from outside its own
subtree. It could already present anything *inside* its subtree, which includes
this hub and every descendant of it, so "a parent can speak as its own children"
was true before the fix and still is. Binding the source on an ancestor link
needs to know which branch a source is reachable through — routing state this
gate does not have.

### The Release teardown fault is not a Java problem either

`FacadeExamples` reports an intermittent access violation during process
teardown in `AlexInteropLight (client)` on Release, at 2-3 runs in 6.
`dotNetExamples` reports the same thing in `AlexInteropNet (client)`, at 4
runs in 6, and concludes the shared factor is the facade/kernel Release teardown
rather than the binding.

Measured here, same method, 6 runs each:

| | Debug | Release |
|---|---|---|
| `AlexInterop (server)` | 0 / 6 | **0 / 6** |
| `AlexInterop (client)` | 0 / 6 | **4 / 6** — `0xC0000005` |

Same side, same configuration, same rate as the C# tree. The fault is raised
*after* the harness printed `Done (exit=0)`, so the test itself completed and its
verdict stands:

```
[03:02:57.309 tid=1 MAIN] shutdown begin
[03:02:57.320 tid=1 MAIN] SUCCESS - handshake completed and the message was sent
Done (exit=0).
                              <- process then dies with 0xC0000005
```

This tree adds one thing to that conclusion. The three previous trees are all
native processes with a CRT the fault could plausibly belong to. This one is a
**JVM** — a completely different host process, a different allocator, a different
shutdown sequence — and it faults on the same side, in the same configuration, at
the same rate. That narrows it further: the remaining shared component is the
facade/kernel Release teardown itself. Still not diagnosed.

The single-process harnesses never fault, in either configuration, in any tree.

### An MFC extension DLL initialises inside a bare JVM

`TargetCore.dll` is an **MFC extension DLL**: its `DllMain` calls
`AfxInitExtensionModule` and `new CDynLinkLibrary(...)`, which need MFC's module
and thread state. `AlexTest/README.md` records the concern that this state is set
up by the `CWinApp theApp` global — and the Light tree's `AlexInterop` resolved
the milder version of the question by testing it: omitting `CWinApp` is benign on
Windows *provided* the DLL is `/DELAYLOAD`ed.

This tree asks the sharp version. A JVM process has no `CWinApp`, no MFC of its
own, no `/DELAYLOAD` linkage, and loads the DLL from a JIT-compiled thread long
after startup. **It works** — first run, no special flags:

```
[02:52:22.960 tid=1  MAIN]   facade : TargetFacade ABI 10 / TargetCore(2026)
[02:52:23.024 tid=21 SERVER] peer up   : WsaMesh.Client
[02:52:23.025 tid=22 CLIENT] peer up   : WsaMesh.Server - loopback TCP connection ready
```

MFC's dynamic runtime supplies a default module state for a process with no
`CWinApp`, and the extension DLL's initialisation is satisfied by it. So the
`CWinApp` requirement is a linkage-order concern, not a hosting one.

### Upcalls arrive on threads the JVM has never seen, and it handles it

The `tid=21` and `tid=22` above are the kernel's own pump threads — created by
`SpawnHub` inside the DLL, with no JVM involvement. Panama attaches them on the
way into an upcall stub, transparently and with no registration. Two consequences
the harnesses have to respect:

* **Handlers for one hub are serialised** (one pump thread per hub), **but hubs
  run concurrently.** `RouteLoopbackTest` has four hubs incrementing shared
  counters from four threads at once, and uses `AtomicInteger` where its C++ twin
  uses `InterlockedIncrement`. Its scenario [3] proves a *negative* by summing all
  four counters before and after, so a lost increment would turn a routing bug
  into a PASS.
* **An exception must never leave a handler.** A Java exception escaping an
  upcall stub does not unwind into C++ — it takes the whole JVM down. Every
  upcall body in `Hub` is wrapped, and a throw from user code is reported to
  stderr and swallowed. This is the Java equivalent of the `_CrtSetReportHook`
  trap the originals carry, and it is needed for the same reason: the failure
  mode is otherwise indistinguishable from a kernel crash.

Compare `dotNetExamples`'s finding #1, which is the opposite shape: a CLR
callable wrapper is *agile*, so the COM layer's Global-Interface-Table
marshalling silently does nothing and handlers run on arbitrary MTA threads.
There is no marshalling layer here to be defeated — the upcall runs on whichever
thread called it, always, and the threading model is therefore the kernel's own
rather than an artefact of the binding.

### The arena holding a sink must be shared, not confined

`Arena.ofConfined()` is the usual default and it is *wrong* here. An upcall stub
lives in its arena's memory, and the thread that calls it is the kernel's pump
thread — a confined arena throws `WrongThreadException` on access from another
thread, and that throw happens *inside an upcall*, where it is fatal rather than
catchable. `Hub` uses `Arena.ofShared()`, and closes it only after
`IP2PHub::Close` has returned, because the facade holds the sink pointer for as
long as the hub is alive.

### The build warns nine times, and the warnings are correct

`javac -Xlint:all` reports every `Linker.downcallHandle`, `Linker.upcallStub` and
`MemorySegment.reinterpret` call as `[restricted]`:

> Restricted methods are unsafe and, if used incorrectly, might crash the Java
> runtime or corrupt memory.

They are not suppressed. Nine warnings is an accurate description of a file whose
job is to hand-assemble calls into a C++ vtable, and a `@SuppressWarnings` on
`Native.java` would hide exactly the property a reader should be told about.
`run_all.ps1` passes `--enable-native-access=ALL-UNNAMED` at run time, which
silences the runtime half — and is *required* from JDK 25 on, where an unnamed
module calling a restricted method is an error rather than a warning.

### A smaller one: redirected stderr comes back UTF-16 on Release

The kernel writes its `[ERROR]` diagnostics narrow on Debug and wide on Release,
so a redirected stderr on Release contains `" [ E R R O R ] "` — UTF-16 bytes read
as if they were bytes. The C++ trees never see this because their `run_all.ps1`
redirects stdout only. It is pre-existing kernel behaviour, harmless, and noted
here because the logs in `logs\Release\*.err.txt` look corrupted and are not.

## Requirements

| | |
|---|---|
| JDK | **22 or later** — `java.lang.foreign` left preview in 22. Verified on Temurin/Oracle **23.0.2**. `build.ps1` refuses anything older. |
| Platform | **x64 Windows only.** See `AlexInterop` above. |
| TargetFacade | built for `x64\<Config>`; `build.ps1` stages `TargetFacade.dll`, `TargetCore.dll` and `Msgcore.dll` into `bin\<Config>` |
| ABI | **10**, and the factory accepts 4–10. Re-check `Abi.java` against `TargetFacade.h` if the facade's ABI moves. |
| `Com232MeshTest` | a com0com null-modem pair on COM5↔COM6 — `setupc install PortName=COM5 PortName=COM6`. Without one it reports SETUP (exit 1), which `run_all.ps1` counts separately from a failure. |

All three DLLs go into `bin\<Config>` *together* and the facade is loaded from
there by full path: Windows resolves a loaded module's own dependencies out of
its directory, which is what keeps `PATH` out of it.

## Layout

```
PanamaJavaExamples\
├── src\mscs\p2pf\          the binding -- the only part that knows about FFI
│   ├── Native.java           load, vtable slot, upcall stub, UTF-16
│   ├── Abi.java              ABI version, SLOT INDICES, HRESULTs, endpoints
│   ├── Network.java          IP2PNetwork
│   └── Hub.java              IP2PHub + the IP2PHubEvents sink Java implements
├── src\mscs\harness\
│   └── Harness.java          Gate, Log, Verdict, the exit-code contract
├── src\mscs\examples\        twelve harnesses, one file each
├── build.ps1                 javac + DLL staging. No pom.xml, no build tool.
├── run_all.ps1               run everything, summarise by exit code
├── bin\<Config>\             staged DLLs (the working directory for a run)
├── out\<Config>\             classes
└── logs\<Config>\            per-harness stdout and stderr
```

There is no `pom.xml` and no build tool, because this tree has **no
dependencies** — not on MSCS and not on anything from Maven Central. `javac` over
a source list is the whole build, and adding a build tool would only obscure that.

## Verified results

Toolchain: JDK 23.0.2 (Temurin 23.0.2+7-58), `javac 23.0.2`, x64.
Facade: `TargetFacade ABI 10 / TargetCore(2026)`, staged from
`TargetFacade\out\x64\<Config>`.

| Harness | Debug | Release |
|---|---|---|
| `WsaMeshTest` | 0 PASS | 0 PASS |
| `PipeMeshTest` | 0 PASS | 0 PASS |
| `DmxMeshTest` | 0 PASS | 0 PASS |
| `Com232MeshTest` | 0 PASS | 0 PASS |
| `LocalInMemoryTest` | 0 PASS | 0 PASS |
| `PipeMsgMapTest` | 0 PASS | 0 PASS |
| `PipeMsgFactoryTest` | 0 PASS | 0 PASS |
| `TwoConTest` | 0 PASS | 0 PASS |
| `RouteLoopbackTest` | 0 PASS | 0 PASS |
| `ExplorerTest` | 0 PASS | 0 PASS |
| `AlexTest (server)` / `(client)` | 0 / 0 PASS | 0 / 0 PASS |
| `AlexInterop (server)` / `(client)` | 0 / 0 PASS | 0 / 0 PASS |
| | **14 passed** | **14 passed** |

Taken after the M2 downward-relay fix; the sibling trees score 13/13 (Light,
Net) and 15/15 (Com) on the same kernel.

The post-verdict Release teardown fault on `AlexInterop (client)`
is measured separately at 4 runs in 6, as above.

### A note on TargetCore's flat C API

These numbers were taken **while** `TargetCore_c.{h,cpp,_u8.cpp}` — the flat
`extern "C"` / Panama surface — was removed from `TargetCore.dll`, and they did
not move. Nothing in this tree used it: Java reaches the kernel through the
facade's vtables, not through that layer, so the symbols it exported were never
on this tree's path.

**It is back in the library, and it is the authoritative copy.**
`MSCS_JavaBindings` generates its bindings by running jextract over that header,
so it has to be the one that is compiled — see `TargetCore/CMakeLists.txt`, which
records the removal and the reversal: *the absence of an in-tree consumer is a
fact about this repository, not about the API; the consumer is out-of-tree by
construction, which is what an FFI surface is for.* It exports **101** entry
points as of 2026-09-08, not the 74 an earlier draft of this note recorded.

Those sources live in `MSCS\TargetCore\`. An earlier draft pointed here instead at
`MSCS_JavaBindings\TargetCore\native\`, which no longer exists: it held a
byte-identical second copy, deleted on 2026-08-14 for the reason second copies
get deleted — the one over there had fallen behind the handle registry, so the
bindings tree documented a library several fixes older than the one it loaded.

The two Java routes bind different things and neither supersedes the other. This
tree calls `TargetFacade.dll` vtable slots through hand-transcribed indices;
`MSCS_JavaBindings` calls `TargetCore.dll` / `Msgcore.dll` flat exports through
generated bindings checked by an ABI coverage gate.

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](../LICENSE) for the
full text.

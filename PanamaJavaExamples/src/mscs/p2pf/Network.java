// Copyright © 2026 Khrustal & Mann
//              MELBOURNE, VICTORIA, AUSTRALIA, 3000
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied. See the License for the specific language governing
// permissions and limitations under the License.
package mscs.p2pf;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.nio.file.Files;
import java.nio.file.Path;

/**
 * {@code IP2PNetwork} — the one object that starts and stops the kernel.
 *
 * <p>Replaces the {@code StartupP2Pmsg(16)} + {@code WSAStartup(2,2)} pair (and
 * their matching teardown on every exit path) that every harness in
 * {@code DirectExamples} carries by hand. Construct one, close it last.
 *
 * <p>Implements {@link AutoCloseable} so a harness can say
 * {@code try (Network net = Network.open()) { ... }} and get the C++ tree's
 * destructor ordering for free — which matters, because hubs must close before
 * the network does.
 */
public final class Network implements AutoCloseable {

    // HRESULT __stdcall P2PF_CreateNetwork(unsigned int abiVersion, IP2PNetwork** out)
    private static final FunctionDescriptor FD_CREATE =
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.JAVA_INT, ValueLayout.ADDRESS);

    private static final FunctionDescriptor FD_RELEASE =        // ULONG (this)
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS);
    private static final FunctionDescriptor FD_VERSION =        // const wchar_t* (this)
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS);
    private static final FunctionDescriptor FD_CREATEHUB =      // HRESULT (this, addr, events, out)
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                                  ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS);
    private static final FunctionDescriptor FD_LINK =           // HRESULT (this, a, b, c)
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                                  ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS);
    private static final FunctionDescriptor FD_SETENDPOINT =    // HRESULT (this, a, b)
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                                  ValueLayout.ADDRESS, ValueLayout.ADDRESS);

    private static final MethodHandle MH_RELEASE     = Native.vfn(FD_RELEASE);
    private static final MethodHandle MH_VERSION     = Native.vfn(FD_VERSION);
    private static final MethodHandle MH_CREATEHUB   = Native.vfn(FD_CREATEHUB);
    private static final MethodHandle MH_LINK        = Native.vfn(FD_LINK);
    private static final MethodHandle MH_SETENDPOINT = Native.vfn(FD_SETENDPOINT);

    private MemorySegment net;

    private Network(MemorySegment net) { this.net = net; }

    /**
     * Locate and load {@code TargetFacade.dll}, then create the network.
     *
     * <p>Search order: the {@code P2PF_DLL} system property, then
     * {@code TargetFacade.dll} beside the running classes, then the sibling
     * {@code TargetFacade\out\x64\<Config>} the other trees stage from. Loading
     * by full path also settles where its own dependencies come from
     * ({@code TargetCore.dll}, {@code Msgcore.dll} and MFC): Windows searches
     * the loaded module's own directory for them, which is why the build stages
     * all three side by side rather than relying on {@code PATH}.
     */
    public static Network open() {
        Native.load(locateDll());
        try (Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(ValueLayout.ADDRESS);
            MethodHandle create = Native.LINKER.downcallHandle(
                    Native.symbol("P2PF_CreateNetwork"), FD_CREATE);
            int hr = (int) create.invokeExact(Abi.ABI_VERSION, out);
            if (Abi.failed(hr)) {
                throw new IllegalStateException(
                        "P2PF_CreateNetwork(" + Abi.ABI_VERSION + ") failed: " + Abi.name(hr)
                        + (hr == Abi.E_ABI_MISMATCH
                           ? " -- this binding is transcribed from a different TargetFacade.h"
                             + " than the DLL was built from; see mscs.p2pf.Abi."
                           : ""));
            }
            Network n = new Network(out.get(ValueLayout.ADDRESS, 0));
            n.selfCheck();
            return n;
        } catch (RuntimeException | Error e) {
            throw e;
        } catch (Throwable t) {
            throw new RuntimeException(t);
        }
    }

    private static Path locateDll() {
        String override = System.getProperty("P2PF_DLL");
        if (override != null && !override.isEmpty()) return Path.of(override);

        Path[] candidates = {
            Path.of("TargetFacade.dll"),
            Path.of("bin", "TargetFacade.dll"),
            Path.of("..", "TargetFacade", "out", "x64", "Debug",   "TargetFacade.dll"),
            Path.of("..", "TargetFacade", "out", "x64", "Release", "TargetFacade.dll"),
        };
        for (Path p : candidates) {
            if (Files.isRegularFile(p)) return p.toAbsolutePath().normalize();
        }
        throw new IllegalStateException(
                "TargetFacade.dll not found. Run build.ps1 to stage it, or pass -DP2PF_DLL=<path>.");
    }

    /**
     * Two calls whose answers are known in advance, made before anything else
     * depends on them being right.
     *
     * <p>Nothing in this binding can detect a wrong vtable index by itself — a
     * bad slot number calls a different method rather than failing. So the first
     * thing done with a fresh network pointer is to read slot
     * {@link Abi.Net#VersionString}, which must come back a non-empty string. A
     * skew of even one slot lands on {@code Release} or {@code Link} instead and
     * produces either a refcount drop or a garbage pointer, both of which this
     * catches at the point of the mistake rather than three calls later.
     */
    private void selfCheck() {
        String v = versionString();
        if (v == null || v.isEmpty()) {
            throw new IllegalStateException(
                    "IP2PNetwork vtable self-check failed: VersionString (slot "
                    + Abi.Net.VersionString + ") answered "
                    + (v == null ? "NULL" : "empty")
                    + ". The slot indices in mscs.p2pf.Abi do not match this DLL.");
        }
    }

    /** The facade's own version banner. */
    public String versionString() {
        try {
            MemorySegment p = (MemorySegment) MH_VERSION.invokeExact(
                    Native.slot(net, Abi.Net.VersionString), net);
            return Native.str(p);
        } catch (Throwable t) { throw new RuntimeException(t); }
    }

    /**
     * Create a hub and spawn its pump thread.
     *
     * <p>{@code CreateHub}, not {@code CreateHubEx}: the extended form exists to
     * pass {@code P2PF_HUB_CALLER_PUMPED} and to hand over an
     * {@link Abi.Events IP2PHubEvents2}, and this binding implements only the
     * four-slot {@code IP2PHubEvents}. Registering a sink that is missing the
     * four extended slots through {@code SetExtEvents} would have the facade
     * call vtable entries that are not there.
     */
    public Hub createHub(String address) {
        Hub hub = new Hub(address);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(ValueLayout.ADDRESS);
            int hr = (int) MH_CREATEHUB.invokeExact(
                    Native.slot(net, Abi.Net.CreateHub), net,
                    Native.wstr(a, address), hub.sinkPointer(), out);
            if (Abi.failed(hr)) {
                hub.discardSink();
                throw new IllegalStateException(
                        "IP2PNetwork::CreateHub(\"" + address + "\") failed: " + Abi.name(hr));
            }
            hub.attach(out.get(ValueLayout.ADDRESS, 0));
            return hub;
        } catch (RuntimeException | Error e) {
            throw e;
        } catch (Throwable t) {
            throw new RuntimeException(t);
        }
    }

    /** Arm both ends of an in-process link in one call. */
    public int link(String listenerAddr, String dialerAddr, String endpoint) {
        try (Arena a = Arena.ofConfined()) {
            return (int) MH_LINK.invokeExact(
                    Native.slot(net, Abi.Net.Link), net,
                    Native.wstr(a, listenerAddr), Native.wstr(a, dialerAddr),
                    Native.wstr(a, endpoint));
        } catch (Throwable t) { throw new RuntimeException(t); }
    }

    /** Register the endpoint an address is reachable at, for later resolution. */
    public int setEndpoint(String address, String endpoint) {
        try (Arena a = Arena.ofConfined()) {
            return (int) MH_SETENDPOINT.invokeExact(
                    Native.slot(net, Abi.Net.SetEndpoint), net,
                    Native.wstr(a, address), Native.wstr(a, endpoint));
        } catch (Throwable t) { throw new RuntimeException(t); }
    }

    /** The raw interface pointer, for a call site this class does not cover. */
    public MemorySegment raw() { return net; }

    @Override public void close() {
        if (net == null) return;
        try {
            int unused = (int) MH_RELEASE.invokeExact(Native.slot(net, Abi.Net.Release), net);
        } catch (Throwable t) {
            throw new RuntimeException(t);
        } finally {
            net = null;
        }
    }
}

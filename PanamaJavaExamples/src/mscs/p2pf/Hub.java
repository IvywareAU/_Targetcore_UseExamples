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
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.Consumer;

/**
 * {@code IP2PHub}, plus the {@code IP2PHubEvents} sink Java implements for it.
 *
 * <p>Replaces, from the original harnesses: the {@code P2PeerHub} subclass, its
 * {@code SpawnHub}/{@code CloseHub} pair, its seven {@code On_Con*} trace
 * overrides, and the {@code BEGIN_P2PeerMsg_MAP} macro block. {@link #onTopic}
 * is the message map; {@link #onPeerUp} is {@code On_ConLoginAck}.
 *
 * <h2>The sink is a vtable Java builds</h2>
 * {@code IP2PHubEvents} is a pure-vtable interface with four methods and no
 * {@code IUnknown} — no {@code QueryInterface}, no {@code AddRef}, no
 * {@code Release}. So an implementation of it is exactly: four function
 * pointers in a row, and a pointer to that row. {@link Native#vtableObject}
 * builds one out of four Panama upcall stubs bound to this object's methods, and
 * the facade cannot tell it from one a C++ compiler emitted.
 *
 * <p>Because each hub's stubs are bound to <em>its own</em> instance, the sink
 * pointer identifies the hub implicitly and there is no registry to keep — the
 * {@code this} argument the facade passes back is ignored.
 *
 * <h2>Threading</h2>
 * Handlers run on the hub's pump thread, never on the thread that registered
 * them, and that thread is one the kernel created — the JVM has never seen it.
 * Panama attaches it on the way in. Two consequences a harness must respect:
 *
 * <ul>
 *   <li>Handlers for ONE hub are serialised (one pump thread), but two hubs run
 *       concurrently. Anything shared between them needs to be written for it —
 *       hence the atomics in the harnesses that count across hubs.</li>
 *   <li><b>An exception must never leave a handler.</b> A Java exception
 *       escaping an upcall stub does not unwind into C++; it takes the whole JVM
 *       down. Every upcall body below is wrapped, and a throw from user code is
 *       reported and swallowed.</li>
 * </ul>
 */
public final class Hub implements AutoCloseable {

    /** What a topic handler receives. Mirrors {@code p2pf::Message}. */
    public static final class Message {
        public final String  source;
        public final String  topic;
        public final byte[]  payload;
        public final boolean broadcast;

        Message(String source, String topic, byte[] payload, boolean broadcast) {
            this.source = source; this.topic = topic;
            this.payload = payload; this.broadcast = broadcast;
        }

        /**
         * Payload-as-text. Valid only if the sender used {@link Hub#sendText},
         * which puts the UTF-16 string INCLUDING its terminator on the wire.
         */
        public String text() {
            if (payload == null || payload.length < 2) return "<no data>";
            String s = new String(payload, StandardCharsets.UTF_16LE);
            int nul = s.indexOf('\0');
            return nul >= 0 ? s.substring(0, nul) : s;
        }
    }

    // ── IP2PHub descriptors ──────────────────────────────────────────────────
    private static final ValueLayout.OfInt  I = ValueLayout.JAVA_INT;
    private static final java.lang.foreign.AddressLayout P = ValueLayout.ADDRESS;

    private static final MethodHandle MH_ARM       = Native.vfn(FunctionDescriptor.of(I, P, P, P));
    private static final MethodHandle MH_SEND      = Native.vfn(FunctionDescriptor.of(I, P, P, P, P, I));
    private static final MethodHandle MH_SENDTEXT  = Native.vfn(FunctionDescriptor.of(I, P, P, P, P));
    private static final MethodHandle MH_BROADCAST = Native.vfn(FunctionDescriptor.of(I, P, P, P, I));
    private static final MethodHandle MH_ADDRESS   = Native.vfn(FunctionDescriptor.of(P, P));
    private static final MethodHandle MH_ISPEERUP  = Native.vfn(FunctionDescriptor.of(I, P, P));
    private static final MethodHandle MH_CLOSE     = Native.vfn(FunctionDescriptor.of(I, P));
    private static final MethodHandle MH_COUNT     = Native.vfn(FunctionDescriptor.of(I, P, P));
    private static final MethodHandle MH_BUF       = Native.vfn(FunctionDescriptor.of(I, P, P, P));
    private static final MethodHandle MH_DISCONN   = Native.vfn(FunctionDescriptor.of(I, P, P));
    private static final MethodHandle MH_GETCON    = Native.vfn(FunctionDescriptor.of(I, P, I, P, P, P, P, P));
    private static final MethodHandle MH_GETEP     = Native.vfn(FunctionDescriptor.of(I, P, P, P, P));

    // ── IP2PHubEvents descriptors (the ones WE implement) ────────────────────
    private static final FunctionDescriptor FD_ONMESSAGE =
            FunctionDescriptor.ofVoid(P, P, P, P, I, ValueLayout.JAVA_BOOLEAN);
    private static final FunctionDescriptor FD_ONPEER =
            FunctionDescriptor.ofVoid(P, P);

    private final String address;
    private final Arena  sinkArena;          // SHARED: the pump thread calls these
    private MemorySegment sink;
    private MemorySegment hub;

    private final Map<String, Consumer<Message>> topics = new ConcurrentHashMap<>();
    private volatile Consumer<Message> fallback;
    private volatile Consumer<String>  peerUp;
    private volatile Consumer<String>  peerDown;
    private volatile Consumer<String>  error;

    Hub(String address) {
        this.address = address;
        // ofShared, not ofConfined: an upcall stub lives in this arena's memory,
        // and the thread that calls it is the kernel's pump thread, not the one
        // that allocated it. A confined arena would throw WrongThreadException
        // inside the stub -- i.e. inside an upcall, which is fatal rather than
        // catchable.
        this.sinkArena = Arena.ofShared();
        this.sink = buildSink();
    }

    private MemorySegment buildSink() {
        try {
            MethodHandles.Lookup lk = MethodHandles.lookup();
            MethodHandle onMessage = lk.findVirtual(Hub.class, "upMessage",
                    MethodType.methodType(void.class, MemorySegment.class, MemorySegment.class,
                            MemorySegment.class, MemorySegment.class, int.class, boolean.class))
                    .bindTo(this);
            MethodHandle onPeerUp = lk.findVirtual(Hub.class, "upPeerUp",
                    MethodType.methodType(void.class, MemorySegment.class, MemorySegment.class))
                    .bindTo(this);
            MethodHandle onPeerDown = lk.findVirtual(Hub.class, "upPeerDown",
                    MethodType.methodType(void.class, MemorySegment.class, MemorySegment.class))
                    .bindTo(this);
            MethodHandle onError = lk.findVirtual(Hub.class, "upError",
                    MethodType.methodType(void.class, MemorySegment.class, MemorySegment.class))
                    .bindTo(this);

            MemorySegment[] slots = new MemorySegment[Abi.Events.SLOTS];
            slots[Abi.Events.OnMessage]  = Native.stub(onMessage,  FD_ONMESSAGE, sinkArena);
            slots[Abi.Events.OnPeerUp]   = Native.stub(onPeerUp,   FD_ONPEER,    sinkArena);
            slots[Abi.Events.OnPeerDown] = Native.stub(onPeerDown, FD_ONPEER,    sinkArena);
            slots[Abi.Events.OnError]    = Native.stub(onError,    FD_ONPEER,    sinkArena);
            return Native.vtableObject(sinkArena, slots);
        } catch (ReflectiveOperationException e) {
            throw new IllegalStateException("could not build the IP2PHubEvents vtable", e);
        }
    }

    MemorySegment sinkPointer()  { return sink; }
    void attach(MemorySegment h) { this.hub = h; }
    void discardSink()           { sink = null; sinkArena.close(); }

    // ── the upcalls ──────────────────────────────────────────────────────────
    //
    // `self` is our own sink pointer coming back; it is ignored, because each
    // stub is already bound to the Hub it belongs to.

    @SuppressWarnings("unused")
    private void upMessage(MemorySegment self, MemorySegment source, MemorySegment topic,
                           MemorySegment payload, int size, boolean broadcast) {
        try {
            String t = Native.str(topic);
            Message m = new Message(Native.str(source), t, Native.bytes(payload, size), broadcast);
            Consumer<Message> h = (t == null) ? null : topics.get(t);
            if (h == null) h = fallback;
            if (h != null) h.accept(m);
        } catch (Throwable t) {
            reportInUpcall("OnMessage", t);
        }
    }

    @SuppressWarnings("unused")
    private void upPeerUp(MemorySegment self, MemorySegment peer) {
        try { Consumer<String> h = peerUp;   if (h != null) h.accept(Native.str(peer)); }
        catch (Throwable t) { reportInUpcall("OnPeerUp", t); }
    }

    @SuppressWarnings("unused")
    private void upPeerDown(MemorySegment self, MemorySegment peer) {
        try { Consumer<String> h = peerDown; if (h != null) h.accept(Native.str(peer)); }
        catch (Throwable t) { reportInUpcall("OnPeerDown", t); }
    }

    @SuppressWarnings("unused")
    private void upError(MemorySegment self, MemorySegment what) {
        try { Consumer<String> h = error;    if (h != null) h.accept(Native.str(what)); }
        catch (Throwable t) { reportInUpcall("OnError", t); }
    }

    /**
     * The last line of defence. Anything that escapes here escapes into C++,
     * and Panama's answer to that is to abort the VM — so a harness bug in a
     * handler would look like a kernel crash. Print it and carry on instead.
     */
    private void reportInUpcall(String which, Throwable t) {
        try {
            System.err.println("[" + address + "] exception escaped " + which
                               + " and was swallowed: " + t);
            t.printStackTrace();
            System.err.flush();
        } catch (Throwable ignored) { /* nothing left to try */ }
    }

    // ── handler registration (the no-macro message map) ──────────────────────

    public Hub onTopic(String topic, Consumer<Message> h) { topics.put(topic, h); return this; }
    public Hub onMessage(Consumer<Message> h)             { fallback = h;  return this; }
    public Hub onPeerUp(Consumer<String> h)               { peerUp   = h;  return this; }
    public Hub onPeerDown(Consumer<String> h)             { peerDown = h;  return this; }
    public Hub onError(Consumer<String> h)                { error    = h;  return this; }

    // ── thin forwards to IP2PHub ─────────────────────────────────────────────

    public int listen(String toPeer, String endpoint)  { return arm(Abi.Hub.Listen,  toPeer, endpoint); }
    public int connect(String toPeer, String endpoint) { return arm(Abi.Hub.Connect, toPeer, endpoint); }

    private int arm(int slot, String toPeer, String endpoint) {
        try (Arena a = Arena.ofConfined()) {
            return (int) MH_ARM.invokeExact(Native.slot(hub, slot), hub,
                    Native.wstr(a, toPeer), Native.wstr(a, endpoint));
        } catch (Throwable t) { throw new RuntimeException(t); }
    }

    public int send(String dest, String topic, byte[] payload) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment buf = (payload == null || payload.length == 0)
                              ? MemorySegment.NULL : a.allocateFrom(ValueLayout.JAVA_BYTE, payload);
            return (int) MH_SEND.invokeExact(Native.slot(hub, Abi.Hub.Send), hub,
                    Native.wstr(a, dest), Native.wstr(a, topic), buf,
                    payload == null ? 0 : payload.length);
        } catch (Throwable t) { throw new RuntimeException(t); }
    }

    public int sendText(String dest, String topic, String text) {
        try (Arena a = Arena.ofConfined()) {
            return (int) MH_SENDTEXT.invokeExact(Native.slot(hub, Abi.Hub.SendText), hub,
                    Native.wstr(a, dest), Native.wstr(a, topic), Native.wstr(a, text));
        } catch (Throwable t) { throw new RuntimeException(t); }
    }

    public int broadcast(String topic, byte[] payload) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment buf = (payload == null || payload.length == 0)
                              ? MemorySegment.NULL : a.allocateFrom(ValueLayout.JAVA_BYTE, payload);
            return (int) MH_BROADCAST.invokeExact(Native.slot(hub, Abi.Hub.Broadcast), hub,
                    Native.wstr(a, topic), buf, payload == null ? 0 : payload.length);
        } catch (Throwable t) { throw new RuntimeException(t); }
    }

    /** Broadcast a UTF-16 string with its terminator, so {@link Message#text} reads it back. */
    public int broadcastText(String topic, String text) {
        return broadcast(topic, withNul(text));
    }

    private static byte[] withNul(String s) {
        return (s + "\0").getBytes(StandardCharsets.UTF_16LE);
    }

    public String address() {
        if (hub == null) return address;
        try {
            MemorySegment p = (MemorySegment) MH_ADDRESS.invokeExact(
                    Native.slot(hub, Abi.Hub.Address), hub);
            return Native.str(p);
        } catch (Throwable t) { throw new RuntimeException(t); }
    }

    public boolean isPeerUp(String peer) {
        try (Arena a = Arena.ofConfined()) {
            return 0 != (int) MH_ISPEERUP.invokeExact(Native.slot(hub, Abi.Hub.IsPeerUp), hub,
                    Native.wstr(a, peer));
        } catch (Throwable t) { throw new RuntimeException(t); }
    }

    public int conCount() {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(ValueLayout.JAVA_INT);
            int hr = (int) MH_COUNT.invokeExact(Native.slot(hub, Abi.Hub.GetConCount), hub, out);
            return Abi.failed(hr) ? 0 : out.get(ValueLayout.JAVA_INT, 0);
        } catch (Throwable t) { throw new RuntimeException(t); }
    }

    public int disconnect(String peer) {
        try (Arena a = Arena.ofConfined()) {
            return (int) MH_DISCONN.invokeExact(Native.slot(hub, Abi.Hub.Disconnect), hub,
                    Native.wstr(a, peer));
        } catch (Throwable t) { throw new RuntimeException(t); }
    }

    /** The hub's own prose description of itself. Two-call, size-then-fill. */
    public String describe() { return sizeThenFill(Abi.Hub.Describe); }

    /** One entry of the peer table: what {@code GetCon} reports at a position. */
    public record Con(String peer, String endpoint, int flags) {
        @Override public String toString() {
            return peer + "\t" + endpoint + "\t" + Abi.conFlags(flags);
        }
    }

    /**
     * Read one peer by position.
     *
     * <p>The set is live -- a login on a pump thread can add a peer between two
     * calls -- so {@code index} is a position in a momentary ordering, never a
     * handle. Code that needs a consistent picture takes one with
     * {@link #describe}.
     *
     * <p>Two calls per buffer, as the facade's buffer protocol requires: NULL to
     * ask the size, then again to fill. Both sizes come back from the first call
     * even though only one buffer is being asked about.
     */
    public Con getCon(int index) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment peerCch = a.allocate(ValueLayout.JAVA_INT);
            MemorySegment epCch   = a.allocate(ValueLayout.JAVA_INT);
            MemorySegment flags   = a.allocate(ValueLayout.JAVA_INT);

            int hr = (int) MH_GETCON.invokeExact(Native.slot(hub, Abi.Hub.GetCon), hub, index,
                    MemorySegment.NULL, peerCch, MemorySegment.NULL, epCch, flags);
            if (Abi.failed(hr)) return null;

            int pn = peerCch.get(ValueLayout.JAVA_INT, 0);
            int en = epCch.get(ValueLayout.JAVA_INT, 0);
            MemorySegment peerBuf = pn > 0 ? a.allocate((long) pn * 2) : MemorySegment.NULL;
            MemorySegment epBuf   = en > 0 ? a.allocate((long) en * 2) : MemorySegment.NULL;

            hr = (int) MH_GETCON.invokeExact(Native.slot(hub, Abi.Hub.GetCon), hub, index,
                    peerBuf, peerCch, epBuf, epCch, flags);
            if (Abi.failed(hr)) return null;

            return new Con(
                    pn > 0 ? peerBuf.getString(0, StandardCharsets.UTF_16LE) : "",
                    en > 0 ? epBuf.getString(0, StandardCharsets.UTF_16LE)   : "",
                    flags.get(ValueLayout.JAVA_INT, 0));
        } catch (Throwable t) { throw new RuntimeException(t); }
    }

    /**
     * The endpoint this hub armed for one peer, canonically spelled -- so it
     * round-trips straight back into {@link #listen}/{@link #connect}. Empty for
     * a peer that is known but was never armed by this hub.
     */
    public String getEndpoint(String peer) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment p   = Native.wstr(a, peer);
            MemorySegment cch = a.allocate(ValueLayout.JAVA_INT);
            int hr = (int) MH_GETEP.invokeExact(Native.slot(hub, Abi.Hub.GetEndpoint), hub,
                    p, MemorySegment.NULL, cch);
            int n = cch.get(ValueLayout.JAVA_INT, 0);
            if (Abi.failed(hr) || n < 2) return "";
            MemorySegment buf = a.allocate((long) n * 2);
            hr = (int) MH_GETEP.invokeExact(Native.slot(hub, Abi.Hub.GetEndpoint), hub,
                    p, buf, cch);
            return Abi.failed(hr) ? "" : buf.getString(0, StandardCharsets.UTF_16LE);
        } catch (Throwable t) { throw new RuntimeException(t); }
    }

    private String sizeThenFill(int slot) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment cch = a.allocate(ValueLayout.JAVA_INT);
            int hr = (int) MH_BUF.invokeExact(Native.slot(hub, slot), hub, MemorySegment.NULL, cch);
            int n = cch.get(ValueLayout.JAVA_INT, 0);
            if (Abi.failed(hr) || n < 2) return "";
            MemorySegment buf = a.allocate((long) n * 2);
            hr = (int) MH_BUF.invokeExact(Native.slot(hub, slot), hub, buf, cch);
            return Abi.failed(hr) ? "" : buf.getString(0, StandardCharsets.UTF_16LE);
        } catch (Throwable t) { throw new RuntimeException(t); }
    }

    public MemorySegment raw() { return hub; }

    /**
     * Close the pump, then release the sink's memory — in that order, and never
     * the other way. The facade holds our sink pointer for as long as the hub is
     * alive; freeing the arena first would leave it calling into unmapped
     * memory on the pump thread.
     */
    @Override public void close() {
        if (hub != null) {
            try { int unused = (int) MH_CLOSE.invokeExact(Native.slot(hub, Abi.Hub.Close), hub); }
            catch (Throwable t) { throw new RuntimeException(t); }
            finally { hub = null; }
        }
        if (sink != null) { sink = null; sinkArena.close(); }
    }
}

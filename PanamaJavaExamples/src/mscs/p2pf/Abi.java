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

/**
 * Everything this binding knows about {@code TargetFacade.h} that it cannot ask
 * the DLL for: the ABI version, the vtable slot indices, and the HRESULTs.
 *
 * <p>This file is the hand-maintained half of the binding, and the only half
 * that can be silently wrong. It is transcribed from
 * {@code TargetFacade/include/TargetFacade.h} and must be re-checked against it
 * whenever the facade's ABI moves. The rules the header states about what may
 * move are worth repeating, because they are what make a transcription viable
 * at all:
 *
 * <ul>
 *   <li>ABI 2, 3, 5, 6, 7, 8, 9 and 10 were <b>append-only</b>: new methods went
 *       after the existing ones, so every index already written here kept its
 *       value.</li>
 *   <li>ABI 4 was a <b>hard cut</b> — eight typed arming verbs were removed and
 *       {@code ListenEx}/{@code ConnectEx} took their names and slots, moving
 *       everything after them. A binding written against ABI 3 does not fail at
 *       {@code CreateNetwork}; it calls the wrong method with the wrong
 *       arguments. That is why {@link #ABI_VERSION} is passed to the factory and
 *       why the factory refuses anything below {@link #ABI_VERSION_MIN}.</li>
 * </ul>
 *
 * <p>A slot index is not checked by anything. {@link Network#selfCheck} exists
 * because of that: it calls two slots whose answers are known in advance, so a
 * transcription error shows up on the first line of output rather than as a
 * corrupted call somewhere later.
 */
public final class Abi {

    private Abi() { }

    /**
     * The version this binding is transcribed from. Passed to
     * {@code P2PF_CreateNetwork}, which accepts {@link #ABI_VERSION_MIN}..this
     * and answers {@link #E_ABI_MISMATCH} otherwise.
     */
    public static final int ABI_VERSION     = 10;
    public static final int ABI_VERSION_MIN = 4;

    // ── IP2PNetwork ──────────────────────────────────────────────────────────
    public static final class Net {
        private Net() { }
        /** HRESULT CreateHub(const wchar_t*, IP2PHubEvents*, IP2PHub**) */
        public static final int CreateHub      = 0;
        /** ULONG Release() */
        public static final int Release        = 1;
        /** const wchar_t* VersionString() const */
        public static final int VersionString  = 2;
        /** HRESULT Link(const wchar_t*, const wchar_t*, const wchar_t*) */
        public static final int Link           = 3;
        /** HRESULT SetEndpoint(const wchar_t*, const wchar_t*) */
        public static final int SetEndpoint    = 4;
        /** HRESULT SetEndpointMap(const wchar_t*, unsigned int*) */
        public static final int SetEndpointMap = 5;
        /** HRESULT GetEndpointFor(const wchar_t*, wchar_t*, unsigned int*) const */
        public static final int GetEndpointFor = 6;
        /** HRESULT CreateMessage(IP2PMessage**) */
        public static final int CreateMessage  = 7;
        /** HRESULT CreateHubEx(const wchar_t*, IP2PHubEvents*, unsigned int, IP2PHub**) */
        public static final int CreateHubEx    = 8;
        /** HRESULT SetDiagSink(IP2PDiagEvents*, unsigned int) */
        public static final int SetDiagSink    = 9;
        public static final int SetDiagMask    = 10;
        public static final int GetDiagMask    = 11;
        public static final int GetDiagText    = 12;
        public static final int GetDiagInfo    = 13;
        public static final int RaiseDiag      = 14;
        public static final int IsDiagWanted   = 15;
    }

    // ── IP2PHub ──────────────────────────────────────────────────────────────
    public static final class Hub {
        private Hub() { }
        /** HRESULT Listen(const wchar_t* toPeer, const wchar_t* endpoint) */
        public static final int Listen        = 0;
        /** HRESULT Connect(const wchar_t* toPeer, const wchar_t* endpoint) */
        public static final int Connect       = 1;
        /** HRESULT Send(const wchar_t* dest, const wchar_t* topic, const void*, unsigned int) */
        public static final int Send          = 2;
        /** HRESULT SendText(const wchar_t* dest, const wchar_t* topic, const wchar_t* text) */
        public static final int SendText      = 3;
        /** HRESULT Broadcast(const wchar_t* topic, const void*, unsigned int) */
        public static final int Broadcast     = 4;
        /** const wchar_t* Address() const */
        public static final int Address       = 5;
        /** BOOL IsPeerUp(const wchar_t* peer) const */
        public static final int IsPeerUp      = 6;
        /** HRESULT Close() */
        public static final int Close         = 7;
        /** HRESULT GetConCount(unsigned int*) const */
        public static final int GetConCount   = 8;
        /** HRESULT GetCon(unsigned int, wchar_t*, unsigned int*, wchar_t*, unsigned int*, unsigned int*) const */
        public static final int GetCon        = 9;
        /** HRESULT GetEndpoint(const wchar_t*, wchar_t*, unsigned int*) const */
        public static final int GetEndpoint   = 10;
        /** HRESULT Describe(wchar_t*, unsigned int*) const */
        public static final int Describe      = 11;
        /** HRESULT Disconnect(const wchar_t*) */
        public static final int Disconnect    = 12;
        /** HRESULT SetExtEvents(IP2PHubEvents2*) */
        public static final int SetExtEvents  = 13;
        public static final int SetTimer      = 14;
        public static final int KillTimer     = 15;
        public static final int Post          = 16;
        public static final int SetConOption  = 17;
        public static final int GetConOption  = 18;
        public static final int Ping          = 19;
        public static final int CloseIdleCons = 20;
        public static final int GetNative     = 21;
        public static final int SendEx        = 22;
        public static final int BroadcastEx   = 23;
        /** HRESULT GetMsgInfo(wchar_t*, unsigned int*, unsigned int*, unsigned int*, unsigned int*) const */
        public static final int GetMsgInfo    = 24;
        public static final int SendMsg       = 25;
        public static final int BroadcastMsg  = 26;
        public static final int GetFieldCount = 27;
        public static final int GetFieldName  = 28;
        public static final int GetField      = 29;
        public static final int Pump          = 30;
        public static final int GetPending    = 31;
        public static final int GetPumpInfo   = 32;
    }

    // ── IP2PHubEvents (WE implement this one) ────────────────────────────────
    //
    // Four slots, in declaration order, and NOT derived from IUnknown: there is
    // no QueryInterface, no AddRef and no Release on it. That is what makes a
    // Java-built sink possible at all -- the facade never asks the sink for
    // another interface and never refcounts it, so a plain vtable of four
    // upcall stubs is a complete implementation.
    public static final class Events {
        private Events() { }
        /** void OnMessage(const wchar_t*, const wchar_t*, const void*, unsigned int, bool) */
        public static final int OnMessage  = 0;
        /** void OnPeerUp(const wchar_t*) */
        public static final int OnPeerUp   = 1;
        /** void OnPeerDown(const wchar_t*) */
        public static final int OnPeerDown = 2;
        /** void OnError(const wchar_t*) */
        public static final int OnError    = 3;
        public static final int SLOTS      = 4;
    }

    // ── GetCon flags ─────────────────────────────────────────────────────────
    //
    // How the connection was armed, whether it is up, and -- the half worth
    // having -- where the peer sits in the dotted address tree relative to this
    // hub. The kernel never checks that a connection joins an ancestor to a
    // descendant, and a wrongly-shaped link connects, logs in and looks healthy,
    // so the REL_ bits turn a silent class of topology bug into one read.
    public static final int CON_LISTEN     = 0x0001;
    public static final int CON_DIAL       = 0x0002;
    public static final int CON_UP         = 0x0004;

    /** Exactly one of these is set. */
    public static final int REL_SELF       = 0x0100;
    public static final int REL_DESCENDANT = 0x0200;
    public static final int REL_ANCESTOR   = 0x0400;
    public static final int REL_UNRELATED  = 0x0800;
    public static final int REL_PATTERN    = 0x1000;
    public static final int REL_MASK       = 0x1F00;

    /** Render a GetCon flag word the way Describe() spells it. */
    public static String conFlags(int flags) {
        StringBuilder sb = new StringBuilder();
        if ((flags & CON_LISTEN) != 0) sb.append("listen,");
        if ((flags & CON_DIAL)   != 0) sb.append("dial,");
        if ((flags & CON_UP)     != 0) sb.append("up,");
        switch (flags & REL_MASK) {
            case REL_SELF       -> sb.append("self");
            case REL_DESCENDANT -> sb.append("descendant");
            case REL_ANCESTOR   -> sb.append("ancestor");
            case REL_UNRELATED  -> sb.append("unrelated");
            case REL_PATTERN    -> sb.append("pattern");
            default             -> sb.append("rel?");
        }
        return sb.toString();
    }

    // ── HRESULTs ─────────────────────────────────────────────────────────────
    //
    // MAKE_HRESULT(1, FACILITY_ITF, code) == 0x80040000 | code, and the S_ codes
    // are MAKE_HRESULT(0, ...) == 0x00040000 | code.
    public static final int S_OK    = 0x00000000;
    public static final int S_FALSE = 0x00000001;

    private static int err(int code) { return 0x80040000 | code; }
    private static int ok (int code) { return 0x00040000 | code; }

    /**
     * A SUCCESS code, not an error: the link IS armed, but the two addresses are
     * neither ancestor nor descendant, so it can never be a transit hop. Every
     * {@code X.Server}/{@code X.Client} pair in this tree is a sibling pair, so
     * this is the expected answer from most arming calls here — and {@link
     * #failed} correctly does not see it.
     */
    public static final int S_UNRELATED_LINK = ok(0x0201);

    public static final int E_ABI_MISMATCH   = err(0x0200);
    public static final int E_STARTUP        = err(0x0201);
    public static final int E_HUB_SPAWN      = err(0x0202);
    public static final int E_CON_FACTORY    = err(0x0203);
    public static final int E_CON_DUPLICATE  = err(0x0204);
    public static final int E_RESERVED_TOPIC = err(0x0205);
    public static final int E_CLOSED         = err(0x0206);
    public static final int E_HUB_DUPLICATE  = err(0x0207);
    public static final int E_ENDPOINT       = err(0x0208);
    public static final int E_UNRESOLVED     = err(0x0209);
    public static final int E_NO_HUB         = err(0x020A);
    public static final int E_LINK_PARTIAL   = err(0x020B);

    public static final int E_INVALIDARG     = 0x80070057;
    public static final int E_OUTOFMEMORY    = 0x8007000E;
    public static final int E_POINTER        = 0x80004003;
    public static final int E_FAIL           = 0x80004005;
    public static final int E_NOINTERFACE    = 0x80004002;

    /** The FAILED() macro. */
    public static boolean failed(int hr) { return hr < 0; }

    /** Report an HRESULT by its facade-specific name. Mirrors {@code light::HrName}. */
    public static String name(int hr) {
        if (hr == S_OK)              return "S_OK";
        if (hr == S_FALSE)           return "S_FALSE";
        if (hr == S_UNRELATED_LINK)  return "P2PF_S_UNRELATED_LINK";
        if (hr == E_ABI_MISMATCH)    return "P2PF_E_ABI_MISMATCH";
        if (hr == E_STARTUP)         return "P2PF_E_STARTUP";
        if (hr == E_HUB_SPAWN)       return "P2PF_E_HUB_SPAWN";
        if (hr == E_CON_FACTORY)     return "P2PF_E_CON_FACTORY";
        if (hr == E_CON_DUPLICATE)   return "P2PF_E_CON_DUPLICATE";
        if (hr == E_RESERVED_TOPIC)  return "P2PF_E_RESERVED_TOPIC";
        if (hr == E_CLOSED)          return "P2PF_E_CLOSED";
        if (hr == E_HUB_DUPLICATE)   return "P2PF_E_HUB_DUPLICATE";
        if (hr == E_ENDPOINT)        return "P2PF_E_ENDPOINT";
        if (hr == E_UNRESOLVED)      return "P2PF_E_UNRESOLVED";
        if (hr == E_NO_HUB)          return "P2PF_E_NO_HUB";
        if (hr == E_LINK_PARTIAL)    return "P2PF_E_LINK_PARTIAL";
        if (hr == E_INVALIDARG)      return "E_INVALIDARG";
        if (hr == E_OUTOFMEMORY)     return "E_OUTOFMEMORY";
        if (hr == E_POINTER)         return "E_POINTER";
        if (hr == E_FAIL)            return "E_FAIL";
        if (hr == E_NOINTERFACE)     return "E_NOINTERFACE";
        return String.format("(other 0x%08X)", hr);
    }

    // ── Endpoint composers ───────────────────────────────────────────────────
    //
    // The facade has ONE arming pair since ABI 4 -- listen(toPeer, endpoint) /
    // connect(toPeer, endpoint) -- and the transport lives in the endpoint
    // STRING rather than in the method name. The harnesses keep their typed
    // constants (a port is still a port in the source), so the composing happens
    // here, once. Mirrors light::TcpListen and friends.

    /** A listen must NOT name a host: the kernel binds INADDR_ANY regardless. */
    public static String tcpListen(int port)             { return "tcp://:" + port; }
    public static String tcpDial(String host, int port)  { return "tcp://" + host + ":" + port; }
    /** The parser takes the pipe name VERBATIM, so a full \\.\pipe\name passes through. */
    public static String pipe(String name)               { return "pipe://" + name; }
    public static String dmx(String serviceName)         { return "dmx://" + serviceName; }
    public static String serial(int comPort)             { return "serial://COM" + comPort; }
}

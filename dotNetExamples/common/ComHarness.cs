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
//
// ComHarness.cs
//
// The C# counterpart of ComExamples\common\ComHarness.h -- the
// client-side layer that makes a raw COM client of TargetCom as pleasant to
// write as a facade client.
//
// The parallel across all four trees is exact:
//
//   p2pf::Network net;                       Com.Network net;
//   auto hub = net.createHub(L"A");          var hub = net.CreateHub("A");
//   hub.onTopic(L"chat", lambda);            hub.OnTopic("chat", lambda);
//   hub.listen(peer, endpoint);              hub.Listen(peer, endpoint);
//   hub.sendText(dst, topic, text);          hub.SendText(dst, topic, text);
//
// WHAT THE CLR DOES FOR FREE, and what it does not.
//
// Free: every BSTR, every SAFEARRAY(VT_UI1), every VARIANT_BOOL, and the whole
// hand-written IDispatch sink. ComHarness.h needs 408 lines and a from-scratch
// IDispatch implementation to reach this API; the interop declarations in
// TargetComInterop.cs plus this file do it with the marshaller doing the work.
// The EventSink below is an ordinary class implementing an ordinary interface.
//
// NOT free, AND NOT THE SAME AS THE C++ TREE: THE THREAD A CALLBACK RUNS ON.
//
// The C++ COM harnesses declare an STA and every event arrives on the main
// thread -- their logs show one tid= throughout. That happens because a C++
// IDispatch sink is apartment-bound: TargetCom parks the sink in the Global
// Interface Table, re-fetches it on its own MTA dispatch thread, and gets back a
// PROXY that marshals the call into the client's apartment.
//
// A managed sink is AGILE. The CLR's CCW aggregates the free-threaded
// marshaler, so the same GIT re-fetch hands the dispatch thread the *identical
// pointer* -- no proxy, no apartment transition. The measured consequence:
//
//     C++  sink : every callback on the main thread     (tid == main)
//     C#   sink : callbacks on the DLL's dispatch threads, one per hub,
//                 CONCURRENTLY with each other and with main
//
// So the STA declaration below is honest about intent but buys less than it
// looks like, and the handlers in these harnesses must be written as if they
// were on a thread pool -- because they are. Every counter a handler touches is
// Interlocked, Gate is Interlocked, and console output is serialised under one
// lock. This is the single most important thing this tree found that the other
// three could not.
//
// The pump stays. It costs nothing, it is what Gate.Wait would need anyway, and
// it is still load-bearing the moment any non-agile object enters the picture --
// which is a change a client can make by accident. Depending on the CCW staying
// agile is how you get a client that hangs on a customer's machine and not on
// yours.
//
// Exit-code contract, identical to all three C++ trees:
//   0 = SUCCESS   1 = SETUP   3 = TIMEOUT / expectation not met

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text;
using System.Threading;

using TargetCom;

namespace Com
{
    // -----------------------------------------------------------------------
    // Console + logging. Same shape as ComHarness.h's Log/LogMessage so the
    // logs of the two trees diff cleanly.
    // -----------------------------------------------------------------------
    public static class Harness
    {
        public const int ExitSuccess = 0;
        public const int ExitSetup   = 1;
        public const int ExitTimeout = 3;

        // The OS thread id, not ManagedThreadId -- so these logs diff directly
        // against the C++ tree's, where the tid= field is GetCurrentThreadId().
        [DllImport("kernel32.dll")]
        private static extern uint GetCurrentThreadId ();

        /// <summary>The thread Main runs on: the STA that owns every COM object here.</summary>
        public static uint MainTid { get; private set; }

        public static uint Tid { get { return GetCurrentThreadId(); } }

        // Handlers can run on several threads at once (see the README on CCW
        // agility), and a log line is three console calls. One lock keeps lines
        // whole; without it the output interleaves mid-line.
        private static readonly object s_out = new object();

        public static void InitConsole ()
        {
            MainTid = GetCurrentThreadId();
            try { Console.OutputEncoding = new UTF8Encoding(false); }
            catch (Exception) { /* stdout is a pipe with no console: leave it */ }
        }

        public static void Log (string role, string format, params object[] args)
        {
            var now = DateTime.Now;
            string line = string.Format("[{0:00}:{1:00}:{2:00}.{3:000} tid={4} {5}] {6}",
                                        now.Hour, now.Minute, now.Second, now.Millisecond,
                                        GetCurrentThreadId(), role,
                                        args.Length == 0 ? format : string.Format(format, args));
            lock (s_out) { Console.WriteLine(line); Console.Out.Flush(); }
        }

        public static void LogMessage (string role, string kind, string source, string text)
        {
            string block = string.Format("\n[{0}] {1} from '{2}':\n  > {3}\n", role, kind, source, text);
            lock (s_out) { Console.WriteLine(block); Console.Out.Flush(); }
        }

        public static void Print (string format, params object[] args)
        {
            string line = args.Length == 0 ? format : string.Format(format, args);
            lock (s_out) { Console.WriteLine(line); Console.Out.Flush(); }
        }

        /// <summary>The COM server is not registered, or a dependency is missing.</summary>
        public static int SetupFailure (string what, int hr)
        {
            Log("MAIN", "SETUP: {0} failed (0x{1:X8} {2})", what, hr, Hr.Name(hr));
            if (hr == Hr.REGDB_E_CLASSNOTREG)
                Print("\nTargetCom is not registered. Run:\n"
                    + "    run_all.ps1            (registers per-user, runs, unregisters)\n"
                    + "or  regsvr32 /n /i:user \"...\\bin\\Debug\\TargetCom.dll\"");
            return ExitSetup;
        }

        public static int Verdict (bool ok, string okMsg, string failMsg)
        {
            Log("MAIN", ok ? "SUCCESS - " + okMsg : "TIMEOUT - " + failMsg);
            int code = ok ? ExitSuccess : ExitTimeout;
            Print("Done (exit={0}).", code);
            return code;
        }
    }

    // -----------------------------------------------------------------------
    // Endpoint composers -- the same five the C++ trees have (light::TcpListen
    // and com::TcpListen), and character for character the same output.
    //
    // The endpoint string is not translated anywhere on the way down: C# hands
    // a string to the marshaller, the marshaller makes it a BSTR, TargetCom
    // passes the BSTR to p2pf::IP2PHub, and the facade's parser is the only
    // thing that ever looks inside it. Four language boundaries, one grammar.
    //
    // TcpListen takes ushort because a port is a port; AlexInterop, whose port
    // comes from argv, composes from an int on purpose so an out-of-range value
    // reaches the parser as P2PF_E_ENDPOINT instead of silently wrapping.
    // -----------------------------------------------------------------------
    public static class Endpoint
    {
        /// <summary>A listen must NOT name a host: the kernel binds INADDR_ANY regardless.</summary>
        public static string TcpListen (ushort port)              { return "tcp://:" + port; }
        public static string TcpDial   (string host, ushort port) { return "tcp://" + host + ":" + port; }
        public static string Pipe      (string pipeName)          { return "pipe://" + pipeName; }
        public static string Dmx       (string serviceName)       { return "dmx://" + serviceName; }
        public static string Serial    (int comPort)              { return "serial://COM" + comPort; }
    }

    // -----------------------------------------------------------------------
    // Apartment -- [STAThread] on Main already did the CoInitializeEx; this only
    // asserts it, because everything below depends on it being true.
    // -----------------------------------------------------------------------
    public static class Apartment
    {
        public static bool IsSta
        {
            get { return Thread.CurrentThread.GetApartmentState() == ApartmentState.STA; }
        }
    }

    // -----------------------------------------------------------------------
    // The pump an STA owes the runtime.
    // -----------------------------------------------------------------------
    public static class Pump
    {
        [StructLayout(LayoutKind.Sequential)]
        private struct MSG
        {
            public IntPtr hwnd;
            public uint   message;
            public IntPtr wParam;
            public IntPtr lParam;
            public uint   time;
            public int    ptX;
            public int    ptY;
        }

        private const uint PM_REMOVE = 0x0001;

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern bool PeekMessageW (out MSG msg, IntPtr hWnd, uint min, uint max, uint remove);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern bool TranslateMessage (ref MSG msg);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr DispatchMessageW (ref MSG msg);

        /// <summary>Drain the queue once. Marshalled calls are delivered here.</summary>
        public static void Once ()
        {
            MSG msg;
            while (PeekMessageW(out msg, IntPtr.Zero, 0, 0, PM_REMOVE))
            {
                TranslateMessage(ref msg);
                DispatchMessageW(ref msg);
            }
        }

        /// <summary>Pump for a fixed span -- used to observe that something did NOT arrive.</summary>
        public static void For (int ms)
        {
            var sw = Stopwatch.StartNew();
            while (sw.ElapsedMilliseconds < ms) { Once(); Thread.Sleep(5); }
        }
    }

    // -----------------------------------------------------------------------
    // Gate -- a "did it happen yet" flag whose Wait PUMPS.
    // -----------------------------------------------------------------------
    public sealed class Gate
    {
        private int m_open;

        public void Open ()   { Interlocked.Exchange(ref m_open, 1); }
        public bool IsOpen    { get { return Interlocked.CompareExchange(ref m_open, 0, 0) != 0; } }

        public bool Wait (int ms)
        {
            var sw = Stopwatch.StartNew();
            for (;;)
            {
                Pump.Once();
                if (IsOpen) return true;
                if (sw.ElapsedMilliseconds > ms) return false;
                Thread.Sleep(5);
            }
        }

        /// <summary>Wait for several gates at once, still pumping.</summary>
        public static bool WaitAll (int ms, params Gate[] gates)
        {
            var sw = Stopwatch.StartNew();
            for (;;)
            {
                Pump.Once();
                bool all = true;
                foreach (var g in gates) if (!g.IsOpen) { all = false; break; }
                if (all) return true;
                if (sw.ElapsedMilliseconds > ms) return false;
                Thread.Sleep(5);
            }
        }
    }

    // -----------------------------------------------------------------------
    // What a topic handler receives. Mirrors p2pf::Message; the payload arrived
    // as a SAFEARRAY(VT_UI1) and the marshaller already made it a byte[].
    // -----------------------------------------------------------------------
    public sealed class Message
    {
        public string Source    { get; private set; }
        public string Topic     { get; private set; }
        public byte[] Payload   { get; private set; }
        public bool   Broadcast { get; private set; }

        public Message (string source, string topic, byte[] payload, bool broadcast)
        {
            Source = source; Topic = topic; Payload = payload; Broadcast = broadcast;
        }

        public int Size { get { return Payload == null ? 0 : Payload.Length; } }

        /// <summary>The payload read as the NUL-terminated UTF-16 SendText puts on the wire.</summary>
        public string Text
        {
            get
            {
                if (Payload == null || Payload.Length < 2) return "<no data>";
                string s = Encoding.Unicode.GetString(Payload);
                int z = s.IndexOf('\0');
                return z >= 0 ? s.Substring(0, z) : s;
            }
        }
    }

    // -----------------------------------------------------------------------
    // EventSink -- the whole of ComHarness.h's hand-written IDispatch, replaced
    // by a class that implements an interface.
    //
    // ClassInterface(None) is the load-bearing attribute. With the default
    // AutoDispatch the CCW would expose an auto-generated class interface as its
    // IDispatch, and DISPIDs 1-4 would resolve against THAT -- the events would
    // be silently misrouted. With None, the CCW's dispatch identity is
    // IP2PHubEvents and the DispIds line up.
    //
    // The measured cost of None: QueryInterface(IID_IDispatch) on this CCW
    // returns E_NOINTERFACE. Only QueryInterface(DIID__IP2PHubEvents) succeeds.
    // A managed sink is therefore connectable ONLY because CP2PHubCom::Advise
    // tries the DIID after IID_IDispatch fails (ComHub.cpp:586). A connection
    // point that asked for IID_IDispatch alone would refuse every C# client.
    //
    // Handlers run on the DLL's dispatch threads (see the header) -- Topics is
    // written during setup and only read here, which is why a plain Dictionary
    // is safe; anything a handler MUTATES is the harness's problem.
    // -----------------------------------------------------------------------
    [ComVisible(true)]
    [ClassInterface(ClassInterfaceType.None)]
    public sealed class EventSink : IP2PHubEvents
    {
        public readonly Dictionary<string, Action<Message>> Topics =
            new Dictionary<string, Action<Message>>(StringComparer.Ordinal);

        public Action<Message> Fallback;
        public Action<string>  PeerUp;
        public Action<string>  PeerDown;
        public Action<string>  Error;

        public void OnMessage (string source, string topic, object payload, bool broadcast)
        {
            var m = new Message(source ?? string.Empty, topic ?? string.Empty,
                                payload as byte[], broadcast);

            Action<Message> handler;
            if (Topics.TryGetValue(m.Topic, out handler) && handler != null) handler(m);
            else if (Fallback != null)                                       Fallback(m);
        }

        public void OnPeerUp   (string peer) { if (PeerUp   != null) PeerUp  (peer ?? string.Empty); }
        public void OnPeerDown (string peer) { if (PeerDown != null) PeerDown(peer ?? string.Empty); }
        public void OnError    (string what) { if (Error    != null) Error   (what ?? string.Empty); }
    }

    // -----------------------------------------------------------------------
    // Hub -- IP2PHubCom plus its connection point, with facade-shaped verbs.
    //
    // Every verb returns an HRESULT as an int, exactly as the C++ tree does, so
    // the two read side by side and so a harness can branch on
    // P2PF_E_CON_DUPLICATE without a try/catch at every call site. Raw exposes
    // the interface itself for the harnesses that want to show the idiomatic
    // shape -- a COMException whose HResult carries the facade's code out.
    // -----------------------------------------------------------------------
    public sealed class Hub : IDisposable
    {
        private IP2PHubCom      m_hub;
        private IConnectionPoint m_cp;
        private EventSink        m_sink;
        private int              m_cookie;

        internal Hub (IP2PHubCom hub)
        {
            m_hub  = hub;
            m_sink = new EventSink();

            var cpc  = (IConnectionPointContainer)hub;
            var diid = typeof(IP2PHubEvents).GUID;
            cpc.FindConnectionPoint(ref diid, out m_cp);
            m_cp.Advise(m_sink, out m_cookie);
        }

        public IP2PHubCom Raw { get { return m_hub; } }

        /// <summary>
        /// Run an interface call and hand back its HRESULT instead of throwing.
        ///
        /// The catch is Exception, not COMException, and that is not laziness --
        /// it is the second thing this tree found. The CLR does not deliver every
        /// failed COM call as a COMException: it rewrites a fixed table of
        /// well-known HRESULTs into CLR exception types before the caller ever
        /// sees them. Measured against this very interface:
        ///
        ///     P2PF_E_CON_DUPLICATE  0x80040204 -> COMException      (custom, passes through)
        ///     DISP_E_TYPEMISMATCH   0x80020005 -> COMException      (not in the table)
        ///     E_INVALIDARG          0x80070057 -> ArgumentException (REWRITTEN)
        ///     E_NOINTERFACE         0x80004002 -> InvalidCastException
        ///
        /// So a C# client that writes `catch (COMException)` around this API is
        /// correct for the facade's own error range and CRASHES on the COM
        /// layer's argument validation -- which is exactly the validation the IDL
        /// added for automation clients. Marshal.GetHRForException recovers the
        /// original code from whichever type the CLR chose.
        /// </summary>
        public static int Call (Action action)
        {
            try { action(); return Hr.S_OK; }
            catch (Exception e) { return Marshal.GetHRForException(e); }
        }

        // --- handler registration ------------------------------------------
        public Hub OnTopic    (string topic, Action<Message> h) { m_sink.Topics[topic] = h; return this; }
        public Hub OnMessage  (Action<Message> h)               { m_sink.Fallback = h;      return this; }
        public Hub OnPeerUp   (Action<string> h)                { m_sink.PeerUp   = h;      return this; }
        public Hub OnPeerDown (Action<string> h)                { m_sink.PeerDown = h;      return this; }
        public Hub OnError    (Action<string> h)                { m_sink.Error    = h;      return this; }

        // --- transports -----------------------------------------------------
        // Two verbs, four transports; compose the endpoint with Com.Endpoint.
        //
        // These return S_OK for a link the C++ trees report as
        // P2PF_S_UNRELATED_LINK, and that is not a bug here -- a `void` interop
        // signature discards a successful HRESULT and there is nothing for
        // Call() to catch. The sibling-link warning reaches this tree on
        // OnError instead. See the header of TargetComInterop.cs.
        public int Listen  (string toPeer, string endpoint) { return Call(() => m_hub.Listen (toPeer, endpoint)); }
        public int Connect (string toPeer, string endpoint) { return Call(() => m_hub.Connect(toPeer, endpoint)); }

        // --- sending ---------------------------------------------------------
        public int SendText (string dest, string topic, string text)
        {
            return Call(() => m_hub.SendText(dest, topic, text));
        }

        /// <summary>byte[] marshals into the payload VARIANT as SAFEARRAY(VT_UI1).</summary>
        public int Send (string dest, string topic, byte[] payload)
        {
            return Call(() => m_hub.Send(dest, topic, payload));
        }

        /// <summary>Delivered comes back as a value; see the IDL note on Broadcast.</summary>
        public int Broadcast (string topic, byte[] payload, out bool delivered)
        {
            bool d = false;
            int hr = Call(() => d = m_hub.Broadcast(topic, payload));
            delivered = d;
            return hr;
        }

        // --- properties -------------------------------------------------------
        public string Address
        {
            get
            {
                string s = string.Empty;
                Call(() => s = m_hub.Address);
                return s;
            }
        }

        public bool IsPeerUp (string peer)
        {
            bool up = false;
            Call(() => up = m_hub.IsPeerUp(peer));
            return up;
        }

        // --- the read side ----------------------------------------------------
        //
        // What Listen and Connect cannot say to this tier, asked afterwards. The
        // arming verbs return S_OK here for an edge the C++ trees see as
        // P2PF_S_UNRELATED_LINK; RelationTo returns the same fact as a value,
        // which no marshaller can discard.

        /// <summary>
        /// P2PConFlag bits for one peer -- what this hub armed, whether it is up,
        /// and where it sits in the address tree. 0 if this hub has never heard
        /// of the peer (the facade's P2PF_E_UNRESOLVED, swallowed like IsPeerUp's
        /// failures are).
        /// </summary>
        public int RelationTo (string peer)
        {
            int flags = 0;
            Call(() => flags = m_hub.RelationTo(peer));
            return flags;
        }

        /// <summary>True if this edge can carry direct traffic but can never be routed THROUGH.</summary>
        public bool IsUnrelated (string peer)
        {
            return (RelationTo(peer) & (int)P2PConFlag.RelUnrelated) != 0;
        }

        public int ConCount
        {
            get { int n = 0; Call(() => n = m_hub.ConCount); return n; }
        }

        public string PeerAt (int index)
        {
            string s = string.Empty;
            Call(() => s = m_hub.PeerAt(index));
            return s;
        }

        public string EndpointFor (string peer)
        {
            string s = string.Empty;
            Call(() => s = m_hub.EndpointFor(peer));
            return s;
        }

        /// <summary>One atomic snapshot of the whole picture -- straight into a log.</summary>
        public string Description
        {
            get { string s = string.Empty; Call(() => s = m_hub.Description); return s; }
        }

        public int Close () { return Call(() => m_hub.Close()); }

        public void Dispose ()
        {
            if (m_cp != null)
            {
                if (m_cookie != 0) { Call(() => m_cp.Unadvise(m_cookie)); m_cookie = 0; }
                Marshal.ReleaseComObject(m_cp);
                m_cp = null;
            }
            if (m_hub != null)
            {
                Call(() => m_hub.Close());
                Marshal.ReleaseComObject(m_hub);
                m_hub = null;
            }
            m_sink = null;
        }
    }

    // -----------------------------------------------------------------------
    // Network -- the coclass, and the hub factory.
    // -----------------------------------------------------------------------
    public sealed class Network : IDisposable
    {
        private IP2PNetworkCom m_net;

        private Network (IP2PNetworkCom net) { m_net = net; }

        /// <summary>
        /// The interface itself, for harnesses that want to show the idiomatic
        /// shape -- an exception rather than an int. Symmetric with Hub.Raw, and
        /// needed since ABI 4: CreateHub's empty-address check is the COM
        /// layer's *only* remaining argument validation, so it is the only place
        /// left to watch the CLR rewrite E_INVALIDARG into an ArgumentException.
        /// See Com232MeshTest.
        /// </summary>
        public IP2PNetworkCom Raw { get { return m_net; } }

        /// <summary>
        /// CoCreateInstance(TargetCom.P2PNetwork), found in the registry at run
        /// time. Returns null and sets hr on failure -- REGDB_E_CLASSNOTREG is
        /// by far the most likely one.
        /// </summary>
        public static Network Create (out int hr)
        {
            try
            {
                var net = (IP2PNetworkCom)(object)new P2PNetworkClass();
                hr = Hr.S_OK;
                return new Network(net);
            }
            catch (Exception e) { hr = Marshal.GetHRForException(e); return null; }
        }

        /// <summary>
        /// Create and start a hub. Returns null and sets hr on failure, so the
        /// call site can be a `using var` -- which puts the Close/Unadvise/
        /// Release sequence back exactly where the C++ tree's destructors had
        /// it, after the verdict has been printed.
        /// </summary>
        public Hub CreateHub (string address, out int hr)
        {
            IP2PHubCom raw = null;
            int h = Hub.Call(() => raw = m_net.CreateHub(address));
            if (Hr.Failed(h)) { hr = h; return null; }

            Hub created = null;
            hr = Hub.Call(() => created = new Hub(raw));
            return Hr.Failed(hr) ? null : created;
        }

        public string VersionString
        {
            get { try { return m_net.VersionString; } catch (COMException) { return string.Empty; } }
        }

        public int MaxPayload
        {
            get { try { return m_net.MaxPayload; } catch (COMException) { return 0; } }
        }

        public void Dispose ()
        {
            if (m_net != null) { Marshal.ReleaseComObject(m_net); m_net = null; }
        }
    }
}

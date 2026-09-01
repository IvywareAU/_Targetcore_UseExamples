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
// TwoConTest.cs  (C# -- TargetCom)
//
// Can a SINGLE hub supervise TWO connections, and what happens when the second
// names a peer the hub already has?
//
// The original answered by reading P2PeerHub.cpp:432-447 and proving it at
// runtime through a TRUE/FALSE from PostP2PeerCon. The facade turned that into
// a named HRESULT. COM carried the same HRESULT out unchanged. This tree closes
// the loop the C++ COM harness could only predict:
//
//     "a VBScript or C# caller sees a COMException whose HResult is
//      0x80040204, and can branch on it"   -- ComExamples\README.md
//
// PART B does exactly that, and prints the exception. There is no version of
// this test that needs the kernel source, and now no version that needs C++.
//
//   PART A (positive) : Listen(PeerA) and Connect(PeerB) on ONE hub -> both
//                       succeed. Reported as S_OK here, where the two C++ trees
//                       print P2PF_S_UNRELATED_LINK for the very same calls:
//                       these addresses are siblings, the facade says so with a
//                       SUCCESS code, and a `void` interop signature discards
//                       successful HRESULTs. The warning still arrives -- look
//                       for the OnError line in the log -- and since the COM
//                       interface grew a read side, PART A also ASKS: RelationTo
//                       and Description report the sibling shape as data, not as
//                       prose on an event this tier had to be wired for.
//   PART B (negative) : a THIRD connection duplicating PeerA, on a DIFFERENT
//                       endpoint -> COMException, HResult 0x80040204. The
//                       different endpoint is the point: the rule keys on the
//                       peer ADDRESS, not on the port or the transport.
//   PART C            : the connection point's own Advise/Unadvise bookkeeping,
//                       where a stale cookie is refused with
//                       CONNECT_E_NOCONNECTION -- also an exception here.
//
// Verdict by EXIT CODE:
//   0 = PASS  : both distinct-peer arms succeeded, the duplicate was refused,
//               and the connection point refused a stale cookie.
//   3 = FAIL  : behaviour differs from the documented contract.
//   1 = SETUP : the COM server is not registered.

using System;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;

using Com;
using TargetCom;

static class TwoConTest
{
    const ushort Port    = 7788;
    const ushort AltPort = 7789;

    const string HubAddr = "TwoConTest.Hub";
    const string SvcPeer = "TwoConTest.PeerA";
    const string CliPeer = "TwoConTest.PeerB";

    [STAThread]
    static int Main ()
    {
        Harness.InitConsole();
        Harness.Print("=== TwoConTest (C#) - one hub, two connections ===");
        Harness.Print("Hub  : {0}\nPeerA: {1} (Listen :{2})\nPeerB: {3} (Connect 127.0.0.1:{2})\n",
                      HubAddr, SvcPeer, Port, CliPeer);

        if (!Apartment.IsSta) return Harness.ExitSetup;

        int hr;
        using var net = Network.Create(out hr);
        if (net == null) return Harness.SetupFailure("CoCreateInstance(TargetCom.P2PNetwork)", hr);

        var gLogin = new Gate();

        using var hub = net.CreateHub(HubAddr, out hr);
        if (hub == null) return Harness.SetupFailure("CreateHub", hr);

        hub.OnPeerUp  (peer => { Harness.Log("HUB", "peer up   : {0}", peer); gLogin.Open(); });
        hub.OnPeerDown(peer => Harness.Log("HUB", "peer down : {0}", peer));
        hub.OnError   (what => Harness.Log("HUB", "error     : {0}", what));

        Harness.Print("Address property reads back as '{0}'\n", hub.Address);

        // ---- PART A ----------------------------------------------------------
        Harness.Print("--- PART A: two connections with DISTINCT peer addresses ---");

        int hrSvc = hub.Listen(SvcPeer, Endpoint.TcpListen(Port));
        Harness.Log("HUB", "Listen ( '{0}', 'tcp://:{1}' )  -> {2}", SvcPeer, Port, Hr.Name(hrSvc));

        int hrCli = hub.Connect(CliPeer, Endpoint.TcpDial("127.0.0.1", Port));
        Harness.Log("HUB", "Connect( '{0}', 'tcp://127.0.0.1:{1}' ) -> {2}", CliPeer, Port, Hr.Name(hrCli));

        bool partA = !Hr.Failed(hrSvc) && !Hr.Failed(hrCli);
        Harness.Print("    => PART A {0}: one hub is driving two connections\n",
                      partA ? "PASS" : "FAIL");

        // Both lines above print S_OK, and both are lying by omission: the facade
        // returned P2PF_S_UNRELATED_LINK for each -- "TwoConTest.Hub" is neither
        // ancestor nor descendant of "TwoConTest.PeerA" or "...PeerB" -- and a
        // `void` interop signature discards a SUCCESS HRESULT. This tree is where
        // that gap was measured. The read side closes it: the same fact, asked
        // for afterwards as a value, which no marshaller can throw away.
        bool siblingA = hub.IsUnrelated(SvcPeer);
        bool siblingB = hub.IsUnrelated(CliPeer);
        Harness.Log("HUB", "RelationTo( '{0}' ) -> 0x{1:X4}{2}",
                    SvcPeer, hub.RelationTo(SvcPeer),
                    siblingA ? "  (unrelated: direct traffic only, no transit)" : "");
        Harness.Log("HUB", "RelationTo( '{0}' ) -> 0x{1:X4}{2}",
                    CliPeer, hub.RelationTo(CliPeer),
                    siblingB ? "  (unrelated: direct traffic only, no transit)" : "");
        Harness.Print("    => the C# tier now reads off a return value what the C++ trees\n"
                    + "       read off the HRESULT, and what OnError says in prose:\n");
        Harness.Print(hub.Description);

        // Informational: over loopback the hub's own dial reaches its own listener.
        bool selfLogin = gLogin.Wait(5000);
        Harness.Log("MAIN", "self-login over loopback: {0}",
                    selfLogin ? "observed" : "not observed (informational only)");

        // ---- PART B ----------------------------------------------------------
        Harness.Print("\n--- PART B: a THIRD connection duplicating PeerA ---");

        // Straight at the interface, to show the shape a C# caller actually gets.
        // A DIFFERENT port, so nothing about the endpoint collides -- only the
        // peer address repeats. That is what is being refused.
        bool partB = false;
        string dup = Endpoint.TcpListen(AltPort);
        try
        {
            hub.Raw.Listen(SvcPeer, dup);
            Harness.Log("HUB", "Listen ( '{0}', '{1}' ) was ACCEPTED - unexpected", SvcPeer, dup);
        }
        catch (COMException e)
        {
            Harness.Log("HUB", "Listen ( '{0}', '{1}' )  -> COMException 0x{2:X8} {3}",
                        SvcPeer, dup, e.HResult, Hr.Name(e.HResult));
            partB = e.HResult == Hr.P2PF_E_CON_DUPLICATE;
        }

        Harness.Print("    => PART B {0}: a duplicate peer address is {1}\n",
                      partB ? "PASS" : "FAIL",
                      partB ? "refused by name (on a free port), out through COM into a catch block"
                            : "NOT refused as documented");

        // ---- PART C : the connection point's own bookkeeping ------------------
        Harness.Print("--- PART C: the connection point refuses a stale cookie ---");

        bool partC = false;
        var cpc  = (IConnectionPointContainer)hub.Raw;
        var diid = typeof(IP2PHubEvents).GUID;

        IConnectionPoint cp;
        cpc.FindConnectionPoint(ref diid, out cp);
        try
        {
            cp.Unadvise(unchecked((int)0xDEADBEEF));
            Harness.Log("HUB", "Unadvise(bogus cookie) -> accepted, unexpected");
        }
        catch (COMException e)
        {
            partC = e.HResult == Hr.CONNECT_E_NOCONNECTION;
            Harness.Log("HUB", "Unadvise(bogus cookie) -> {0}",
                        partC ? "CONNECT_E_NOCONNECTION" : "(other)");
        }
        Marshal.ReleaseComObject(cp);

        var iidUnknown = new Guid("00000000-0000-0000-C000-000000000046");
        try
        {
            IConnectionPoint bogus;
            cpc.FindConnectionPoint(ref iidUnknown, out bogus);
            Harness.Log("HUB", "FindConnectionPoint(IID_IUnknown) -> found one, unexpected");
            partC = false;
        }
        catch (COMException e)
        {
            bool refused = e.HResult == Hr.CONNECT_E_NOCONNECTION;
            Harness.Log("HUB", "FindConnectionPoint(IID_IUnknown) -> {0}",
                        refused ? "CONNECT_E_NOCONNECTION" : "(other)");
            partC = partC && refused;
        }

        Harness.Print("    => PART C {0}\n", partC ? "PASS" : "FAIL");

        Harness.Log("MAIN", "shutdown begin");
        bool ok = partA && partB && partC;
        return Harness.Verdict(ok,
                               "two connections on one hub, duplicate peer refused, cookies checked",
                               "behaviour differs from the documented contract");
    }
}

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
// PipeMsgFactoryTest.cs  (C# -- TargetCom)
//
// The same HubPing -> HubPong round trip as PipeMsgMapTest (C#), with the SEND
// SITE changed -- a caller-built BINARY payload instead of text.
//
// WHERE THE ORIGINAL WENT. The original demonstrated P2PeerMsg::RedirectFactory
// (and warned at length why not ResponseFactory, which inherits the request's
// routing prefix and loops the reply back into the sender's own map). The
// facade replaced that whole family with one send verb. COM narrowed it once
// more, to a VARIANT:
//
//     hub.Send(dest, topic, payload)      where payload is a VARIANT
//
// The VARIANT is the point, and C# is where it pays off most visibly: the
// parameter is declared `[MarshalAs(UnmanagedType.Struct)] object`, so
//
//     byte[]  -> SAFEARRAY(VT_UI1)   accepted, and what this file sends
//     string  -> VT_BSTR             accepted, which is how VBScript sends
//     int     -> VT_I4               DISP_E_TYPEMISMATCH
//
// with the marshaller building each VARIANT and no cast, SafeArrayCreateVector
// or VariantClear anywhere in this file. The last check proves the refusal.
//
// Ownership, for comparison across four trees: the factory returned a heap
// message the caller owned and PostP2PeerMsg then took -- a leak or a
// double-free lived in that handoff. ComHarness.h freed a SAFEARRAY by hand.
// Here the byte[] is garbage-collected and the interop SAFEARRAY is created and
// destroyed by the marshaller inside the call.
//
// THREADING: s_replyGood is written on the client hub's dispatch thread and read
// on main, so it is volatile. In the C++ COM tree it was a plain bool captured
// by a lambda that only ever ran on the STA. See common\ComHarness.cs.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : the struct round-tripped with its sequence incremented, and a
//                 bad payload type was refused.
//   3 = TIMEOUT : the round trip did not complete in time.
//   1 = SETUP   : the COM server is not registered, or a hub could not be armed.

using System;
using System.Runtime.InteropServices;

using Com;
using TargetCom;

static class PipeMsgFactoryTest
{
    const string PipeName   = @"\\.\pipe\P2PmsgFactoryProbeNet";
    const string ServerAddr = "MsgFac.Server";
    const string ClientAddr = "MsgFac.Client";

    const string MsgPing   = "HubPing";
    const string MsgPong   = "HubPong";
    const string MsgScript = "HubScripted";      // the string-payload probe below

    const uint ProbeMagic = 0x50524F42;         // 'PROB'

    // Byte-identical to the C++ tree's struct Probe: 4 + 4 + 48*sizeof(wchar_t).
    [StructLayout(LayoutKind.Sequential, Pack = 1, CharSet = CharSet.Unicode)]
    struct Probe
    {
        public uint Magic;
        public uint Sequence;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 48)]
        public string Note;
    }

    static readonly int ProbeSize = Marshal.SizeOf(typeof(Probe));

    static byte[] ToBytes (Probe p)
    {
        var bytes = new byte[ProbeSize];
        var h = GCHandle.Alloc(bytes, GCHandleType.Pinned);
        try { Marshal.StructureToPtr(p, h.AddrOfPinnedObject(), false); }
        finally { h.Free(); }
        return bytes;
    }

    static Probe FromBytes (byte[] bytes)
    {
        var h = GCHandle.Alloc(bytes, GCHandleType.Pinned);
        try { return (Probe)Marshal.PtrToStructure(h.AddrOfPinnedObject(), typeof(Probe)); }
        finally { h.Free(); }
    }

    static volatile bool s_replyGood;

    [STAThread]
    static int Main ()
    {
        Harness.InitConsole();
        Harness.Print("=== PipeMsgFactoryTest (C#) - binary payloads through a VARIANT ===");
        Harness.Print("Pipe   : {0}", PipeName);
        Harness.Print("Payload: struct Probe ({0} bytes) as SAFEARRAY(VT_UI1)\n", ProbeSize);

        if (!Apartment.IsSta) return Harness.ExitSetup;

        int hr;
        using var net = Network.Create(out hr);
        if (net == null) return Harness.SetupFailure("CoCreateInstance(TargetCom.P2PNetwork)", hr);
        Harness.Print("        MaxPayload = {0} bytes\n", net.MaxPayload);

        var gDone = new Gate();

        // ---- SERVER: receives the struct, bumps it, sends it back -----------
        using var server = net.CreateHub(ServerAddr, out hr);
        if (server == null) return Harness.SetupFailure("CreateHub(server)", hr);

        server.OnTopic(MsgPing, m =>
        {
            if (m.Size != ProbeSize)
            {
                Harness.Log("SERVER", "unexpected payload size {0}", m.Size);
                return;
            }

            Probe inbound = FromBytes(m.Payload);
            Harness.Print("\n[SERVER] topic '{0}'  from='{1}'  magic={2:X8} seq={3}\n  > {4}\n",
                          m.Topic, m.Source, inbound.Magic, inbound.Sequence, inbound.Note);

            var outbound = new Probe
            {
                Magic    = inbound.Magic,
                Sequence = inbound.Sequence + 1,
                Note     = "Pong: server bumped your sequence."
            };

            int h = server.Send(m.Source, MsgPong, ToBytes(outbound));
            Harness.Log("SERVER", "replied '{0}' -> '{1}' (seq {2}) : {3}",
                        MsgPong, m.Source, outbound.Sequence, Hr.Name(h));
        });
        // The other half of the VARIANT story: a payload that came in as VT_BSTR
        // rather than SAFEARRAY(VT_UI1) still arrives as bytes, and reads back as
        // the string the sender passed.
        server.OnTopic(MsgScript, m =>
            Harness.Log("SERVER", "topic '{0}' : {1} bytes, reads as \"{2}\"", m.Topic, m.Size, m.Text));

        server.OnPeerUp(peer => Harness.Log("SERVER", "peer up : {0}", peer));
        server.OnError (what => Harness.Log("SERVER", "error   : {0}", what));

        hr = server.Listen(ClientAddr, Endpoint.Pipe(PipeName));
        if (Hr.Failed(hr)) return Harness.SetupFailure("Listen(pipe)", hr);

        // ---- CLIENT: sends the struct, verifies what comes back --------------
        using var client = net.CreateHub(ClientAddr, out hr);
        if (client == null) return Harness.SetupFailure("CreateHub(client)", hr);

        client.OnTopic(MsgPong, m =>
        {
            if (m.Size != ProbeSize)
            {
                Harness.Log("CLIENT", "unexpected payload size {0}", m.Size);
                gDone.Open();
                return;
            }

            Probe inbound = FromBytes(m.Payload);
            Harness.Print("\n[CLIENT] topic '{0}'  from='{1}'  magic={2:X8} seq={3}\n  > {4}\n",
                          m.Topic, m.Source, inbound.Magic, inbound.Sequence, inbound.Note);

            s_replyGood = inbound.Magic == ProbeMagic && inbound.Sequence == 2;
            gDone.Open();
        });

        client.OnPeerUp(peer =>
        {
            Harness.Log("CLIENT", "peer up : {0} - pipe ready, sending ping", peer);

            var ping = new Probe
            {
                Magic    = ProbeMagic,
                Sequence = 1,
                Note     = "Ping: hello Server, this is Client."
            };

            int h = client.Send(ServerAddr, MsgPing, ToBytes(ping));
            Harness.Log("CLIENT", "sent '{0}' -> '{1}' (seq {2}) : {3}",
                        MsgPing, ServerAddr, ping.Sequence, Hr.Name(h));
        });
        client.OnError(what => Harness.Log("CLIENT", "error   : {0}", what));

        hr = client.Connect(ServerAddr, Endpoint.Pipe(PipeName));
        if (Hr.Failed(hr)) return Harness.SetupFailure("Connect(pipe)", hr);

        Harness.Log("MAIN", "waiting up to 10s for the binary round trip (pumping)...");
        bool ok = gDone.Wait(10000) && s_replyGood;

        // ---- What the VARIANT will and will not accept ------------------------
        // Straight at the interface, so the refusal shows in its native C# form.
        int hrBad = Hr.S_OK;
        try
        {
            client.Raw.Send(ServerAddr, MsgPing, 42);            // boxes to VT_I4
            Harness.Log("MAIN", "Send(payload = int) was ACCEPTED - unexpected");
        }
        catch (COMException e)
        {
            hrBad = e.HResult;
            Harness.Log("MAIN", "Send(payload = int) -> COMException 0x{0:X8} {1}", e.HResult, Hr.Name(e.HResult));
        }
        ok = ok && hrBad == Hr.DISP_E_TYPEMISMATCH;

        // ...and a string, which is the shape a scripting client is stuck with.
        int hrText = Hub.Call(() => client.Raw.Send(ServerAddr, MsgScript, "a script would send this"));
        Harness.Log("MAIN", "Send(payload = string) -> {0} (VT_BSTR is accepted too)", Hr.Name(hrText));
        Pump.For(500);                                  // let it land, so the log shows it arriving

        Harness.Log("MAIN", "shutdown begin");
        return Harness.Verdict(ok,
                               "the struct round-tripped with its sequence incremented",
                               "no valid reply delivered (round trip did not complete)");
    }
}

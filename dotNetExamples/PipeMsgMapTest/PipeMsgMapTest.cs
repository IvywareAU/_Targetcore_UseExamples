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
// PipeMsgMapTest.cs  (C# -- TargetCom)
//
// TWO HUBS, ONE PROCESS, NAMED PIPE, request/response routed BY MESSAGE NAME.
//
// The chain this harness completes is the reason the whole stack exists, and
// this tree is its last link:
//
//   Targetcore   BEGIN_P2PeerMsg_MAP / ON_P2PeerMsg / END_P2PeerMsg_MAP
//                on a P2PeerHub subclass, plus DECLARE_P2PeerMsg_MAP
//   facade       hub.onTopic(name, lambda)          -- no macros, no subclass
//   COM          one _IP2PHubEvents.OnMessage event carrying (source, topic,
//                payload, broadcast), dispatched by topic on the client side
//   C#           the same one event, arriving as an ordinary interface method
//                call on an ordinary class
//
// The COM step is where the map stopped being a language feature; the C# step is
// where it stops being COM as well. EventSink.OnMessage is a method, the topic
// registry is a Dictionary, and nothing in this file names a DISPID, a VARIANT
// or an apartment. That is the end state the macro map was always approximating.
//
//   client --HubPing--> [pipe] --> server's HubPing handler
//   server --HubPong--> [pipe] --> client's HubPong handler  => DONE
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : the client received the server's HubPong reply, AND the
//                 reserved-namespace rule was enforced.
//   3 = TIMEOUT : the round trip did not complete in time.
//   1 = SETUP   : the COM server is not registered, or a hub could not be armed.

using System;

using Com;
using TargetCom;

static class PipeMsgMapTest
{
    const string PipeName   = @"\\.\pipe\P2PmsgMapProbeNet";
    const string ServerAddr = "MsgMap.Server";
    const string ClientAddr = "MsgMap.Client";

    const string MsgPing = "HubPing";
    const string MsgPong = "HubPong";

    [STAThread]
    static int Main ()
    {
        Harness.InitConsole();
        Harness.Print("=== PipeMsgMapTest (C#) - named-message routing over one event ===");
        Harness.Print("Pipe   : {0}", PipeName);
        Harness.Print("Server : {0}\nClient : {1}\n", ServerAddr, ClientAddr);

        if (!Apartment.IsSta) return Harness.ExitSetup;

        int hr;
        using var net = Network.Create(out hr);
        if (net == null) return Harness.SetupFailure("CoCreateInstance(TargetCom.P2PNetwork)", hr);

        var gDone = new Gate();

        // ---- SERVER: intercepts HubPing, replies with HubPong ---------------
        using var server = net.CreateHub(ServerAddr, out hr);
        if (server == null) return Harness.SetupFailure("CreateHub(server)", hr);

        server.OnTopic(MsgPing, m =>
        {
            Harness.Print("\n[SERVER] topic '{0}'  from='{1}'\n  > {2}\n", m.Topic, m.Source, m.Text);

            int h = server.SendText(m.Source, MsgPong, "Pong: server got your ping.");
            Harness.Log("SERVER", "replied '{0}' -> '{1}' : {2}", MsgPong, m.Source, Hr.Name(h));
        });
        server.OnPeerUp(peer => Harness.Log("SERVER", "peer up : {0}", peer));
        server.OnError (what => Harness.Log("SERVER", "error   : {0}", what));

        hr = server.Listen(ClientAddr, Endpoint.Pipe(PipeName));
        if (Hr.Failed(hr)) return Harness.SetupFailure("Listen(pipe)", hr);

        // ---- CLIENT: sends HubPing, intercepts HubPong -----------------------
        using var client = net.CreateHub(ClientAddr, out hr);
        if (client == null) return Harness.SetupFailure("CreateHub(client)", hr);

        client.OnTopic(MsgPong, m =>
        {
            Harness.Print("\n[CLIENT] topic '{0}'  from='{1}'\n  > {2}\n", m.Topic, m.Source, m.Text);
            gDone.Open();
        });

        // Anything with no registered topic lands here rather than being dropped
        // -- the equivalent of falling through the map to On_P2PeerUCast.
        client.OnMessage(m => Harness.Log("CLIENT", "unrouted topic '{0}' from '{1}'", m.Topic, m.Source));

        client.OnPeerUp(peer =>
        {
            Harness.Log("CLIENT", "peer up : {0} - pipe ready, sending ping", peer);
            int h = client.SendText(ServerAddr, MsgPing, "Ping: hello Server, this is Client.");
            Harness.Log("CLIENT", "sent '{0}' -> '{1}' : {2}", MsgPing, ServerAddr, Hr.Name(h));
        });
        client.OnError(what => Harness.Log("CLIENT", "error   : {0}", what));

        hr = client.Connect(ServerAddr, Endpoint.Pipe(PipeName));
        if (Hr.Failed(hr)) return Harness.SetupFailure("Connect(pipe)", hr);

        Harness.Log("MAIN", "waiting up to 10s for the HubPing -> HubPong round trip (pumping)...");
        bool ok = gDone.Wait(10000);

        // ---- The reserved namespace, as an HRESULT a caller can branch on ----
        int hrReserved = client.SendText(ServerAddr, "P2Pmsg_Nope", "x");
        Harness.Log("MAIN", "topic 'P2Pmsg_Nope' -> {0} (kernel-reserved namespace)", Hr.Name(hrReserved));
        ok = ok && hrReserved == Hr.P2PF_E_RESERVED_TOPIC;

        Harness.Log("MAIN", "shutdown begin");
        return Harness.Verdict(ok,
                               "client received the server's HubPong reply",
                               "no reply delivered (routing did not complete)");
    }
}

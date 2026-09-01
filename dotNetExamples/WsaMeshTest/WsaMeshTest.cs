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
// WsaMeshTest.cs  (C# -- TargetCom)
//
// SINGLE-PROCESS, TWO-HUB LOOPBACK-TCP connectivity probe, driven entirely
// through COM from managed code.
//
// Same question and same verdict as the other three trees. What is different is
// again not the messaging -- it is the build-time relationship with MSCS:
//
//   DirectExamples       links TargetCore.lib + Msgcore.lib, includes four
//                          kernel headers, needs MFC
//   FacadeExamples  links TargetFacade.lib, includes one header
//   ComExamples    links NOTHING. ole32/oleaut32/uuid and a type library
//   dotNetExamples    references NOTHING but mscorlib/System. There is no
//                          MSCS input to the compiler at all -- only the
//                          [ComImport] declarations in common\, and the coclass
//                          is found in the registry at run time.
//
// This runs single-threaded-apartment on purpose, as the C++ COM harness does.
// Watch the tid= field: in the C++ tree every callback lands on the main thread,
// because a C++ sink is apartment-bound and TargetCom's GIT re-fetch produces a
// marshalling proxy. Here they land on the DLL's own dispatch threads, because a
// CLR callable wrapper is agile and the same re-fetch hands back the identical
// pointer. common\ComHarness.cs has the measurement and the consequences.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : the server hub received the client's message over loopback TCP.
//   3 = TIMEOUT : handshake or delivery did not complete in time.
//   1 = SETUP   : the COM server is not registered, or a hub could not be armed.

using System;

using Com;
using TargetCom;

static class WsaMeshTest
{
    const ushort TestPort   = 7801;
    const string ServerAddr = "WsaMesh.Server";
    const string ClientAddr = "WsaMesh.Client";
    const string Topic      = "mesh";

    [STAThread]
    static int Main ()
    {
        Harness.InitConsole();
        Harness.Print("=== WsaMeshTest (C#) - two hubs, one process, loopback TCP ===");
        Harness.Print("Port : {0} (127.0.0.1)   apartment: STA\n", TestPort);

        if (!Apartment.IsSta) return Harness.ExitSetup;

        int hr;
        using var net = Network.Create(out hr);
        if (net == null) return Harness.SetupFailure("CoCreateInstance(TargetCom.P2PNetwork)", hr);
        Harness.Print("        {0}\n", net.VersionString);

        var gDone = new Gate();

        // ---- Hub A: SERVER --------------------------------------------------
        using var server = net.CreateHub(ServerAddr, out hr);
        if (server == null) return Harness.SetupFailure("CreateHub(server)", hr);

        server.OnTopic(Topic, m =>
        {
            Harness.LogMessage("SERVER", "message", m.Source, m.Text);
            gDone.Open();
        });
        server.OnPeerUp  (peer => Harness.Log("SERVER", "peer up   : {0}", peer));
        server.OnPeerDown(peer => Harness.Log("SERVER", "peer down : {0}", peer));
        server.OnError   (what => Harness.Log("SERVER", "error     : {0}", what));

        hr = server.Listen(ClientAddr, Endpoint.TcpListen(TestPort));
        if (Hr.Failed(hr)) return Harness.SetupFailure("Listen", hr);
        Harness.Log("SERVER", "listening for '{0}' on port {1}", ClientAddr, TestPort);

        // ---- Hub B: CLIENT --------------------------------------------------
        using var client = net.CreateHub(ClientAddr, out hr);
        if (client == null) return Harness.SetupFailure("CreateHub(client)", hr);

        client.OnPeerUp(peer =>
        {
            Harness.Log("CLIENT", "peer up   : {0} - loopback TCP connection ready", peer);
            int hrSend = client.SendText(ServerAddr, Topic, "Hello over loopback TCP, through COM from C#!");
            Harness.Log("CLIENT", "SendText  : {0}", Hr.Name(hrSend));
        });
        client.OnPeerDown(peer => Harness.Log("CLIENT", "peer down : {0}", peer));
        client.OnError   (what => Harness.Log("CLIENT", "error     : {0}", what));

        hr = client.Connect(ServerAddr, Endpoint.TcpDial("127.0.0.1", TestPort));
        if (Hr.Failed(hr)) return Harness.SetupFailure("Connect", hr);
        Harness.Log("CLIENT", "dialling 127.0.0.1:{0} (retries until answered)", TestPort);

        // ---- Wait. Gate.Wait pumps while it polls -- see ComHarness.cs on why
        //      that stays even though an agile sink does not strictly need it.
        Harness.Log("MAIN", "waiting up to 10s for connect + delivery (pumping)...");
        bool ok = gDone.Wait(10000);

        Harness.Log("MAIN", "shutdown begin");
        return Harness.Verdict(ok,
                               "server received the client's message over loopback TCP",
                               "no message delivered (handshake did not complete)");
    }
}

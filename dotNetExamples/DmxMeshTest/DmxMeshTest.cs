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
// DmxMeshTest.cs  (C# -- TargetCom)
//
// SINGLE-PROCESS, TWO-HUB in-process CONNECTION probe (Dmx transport), over COM
// from managed code.
//
// A dmx:// dial does NOT retry -- a missing in-process service is a
// configuration fault, not a timing one -- so the listener is armed first.
//
// That rule used to be documented on IP2PHubCom's ListenDmx/ConnectDmx
// helpstrings in the IDL, which meant it reached an object browser even though
// it never reached the [ComImport] declarations in TargetComInterop.cs
// (helpstrings have no managed counterpart -- the one thing a hand-written
// interop loses against TlbImp).
//
// Since ABI 4 it does not reach the object browser either: with four transports
// sharing one Listen/Connect pair there is no per-transport helpstring left to
// hang it on. So the comment below is now the primary source rather than a
// restatement, which is a small real cost of collapsing the verbs.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : the server hub received the client's message over Dmx.
//   3 = TIMEOUT : handshake or delivery did not complete in time.
//   1 = SETUP   : the COM server is not registered, or a hub could not be armed.

using System;

using Com;
using TargetCom;

static class DmxMeshTest
{
    const string ServiceName = "P2PdmxProbeNet";
    const string ServerAddr  = "DmxMesh.Server";
    const string ClientAddr  = "DmxMesh.Client";
    const string Topic       = "mesh";

    [STAThread]
    static int Main ()
    {
        Harness.InitConsole();
        Harness.Print("=== DmxMeshTest (C#) - two hubs, one process, Dmx transport ===");
        Harness.Print("Service : {0}   apartment: STA\n", ServiceName);

        if (!Apartment.IsSta) return Harness.ExitSetup;

        int hr;
        using var net = Network.Create(out hr);
        if (net == null) return Harness.SetupFailure("CoCreateInstance(TargetCom.P2PNetwork)", hr);

        var gDone = new Gate();

        // ---- SERVER: armed FIRST --------------------------------------------
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

        hr = server.Listen(ClientAddr, Endpoint.Dmx(ServiceName));
        if (Hr.Failed(hr)) return Harness.SetupFailure("Listen(dmx)", hr);
        Harness.Log("SERVER", "Dmx service '{0}' armed for '{1}'", ServiceName, ClientAddr);

        // ---- CLIENT -----------------------------------------------------------
        using var client = net.CreateHub(ClientAddr, out hr);
        if (client == null) return Harness.SetupFailure("CreateHub(client)", hr);

        client.OnPeerUp(peer =>
        {
            Harness.Log("CLIENT", "peer up   : {0} - Dmx connection ready", peer);
            int hrSend = client.SendText(ServerAddr, Topic,
                                         "Hello over Dmx, through COM from C# - no OS handle anywhere!");
            Harness.Log("CLIENT", "SendText  : {0}", Hr.Name(hrSend));
        });
        client.OnPeerDown(peer => Harness.Log("CLIENT", "peer down : {0}", peer));
        client.OnError   (what => Harness.Log("CLIENT", "error     : {0}", what));

        hr = client.Connect(ServerAddr, Endpoint.Dmx(ServiceName));
        if (Hr.Failed(hr)) return Harness.SetupFailure("Connect(dmx)", hr);
        Harness.Log("CLIENT", "dialled the Dmx service");

        Harness.Log("MAIN", "waiting up to 10s for connect + delivery (pumping)...");
        bool ok = gDone.Wait(10000);

        Harness.Log("MAIN", "shutdown begin");
        return Harness.Verdict(ok,
                               "server received the client's message over Dmx",
                               "no message delivered (handshake did not complete)");
    }
}

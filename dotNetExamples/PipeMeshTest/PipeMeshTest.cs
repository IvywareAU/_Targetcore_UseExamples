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
// PipeMeshTest.cs  (C# -- TargetCom)
//
// SINGLE-PROCESS, TWO-HUB named-pipe connectivity probe, over COM from managed
// code.
//
// ONE STRING changed from WsaMeshTest and nothing else -- the same relationship
// the other three trees have between these two harnesses, and it got smaller:
// the two used to differ by a verb pair, and now differ by the scheme in a
// string.
//
// The transport never leaked into the interop surface, and now it cannot: there
// is no per-transport member left to declare. `Com.Endpoint.Pipe(name)` is
// ordinary string concatenation in ordinary managed code, so an endpoint can
// come from app.config or argv without this file changing shape at all.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : the server hub received the client's message over the pipe.
//   3 = TIMEOUT : handshake or delivery did not complete in time.
//   1 = SETUP   : the COM server is not registered, or a hub could not be armed.

using System;

using Com;
using TargetCom;

static class PipeMeshTest
{
    const string PipeName   = @"\\.\pipe\P2PmeshProbeNet";
    const string ServerAddr = "PipeMesh.Server";
    const string ClientAddr = "PipeMesh.Client";
    const string Topic      = "mesh";

    [STAThread]
    static int Main ()
    {
        Harness.InitConsole();
        Harness.Print("=== PipeMeshTest (C#) - two hubs, one process, named pipe ===");
        Harness.Print("Pipe : {0}   apartment: STA\n", PipeName);

        if (!Apartment.IsSta) return Harness.ExitSetup;

        int hr;
        using var net = Network.Create(out hr);
        if (net == null) return Harness.SetupFailure("CoCreateInstance(TargetCom.P2PNetwork)", hr);

        var gDone = new Gate();

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

        hr = server.Listen(ClientAddr, Endpoint.Pipe(PipeName));
        if (Hr.Failed(hr)) return Harness.SetupFailure("Listen(pipe)", hr);
        Harness.Log("SERVER", "pipe armed for '{0}'", ClientAddr);

        using var client = net.CreateHub(ClientAddr, out hr);
        if (client == null) return Harness.SetupFailure("CreateHub(client)", hr);

        client.OnPeerUp(peer =>
        {
            Harness.Log("CLIENT", "peer up   : {0} - pipe ready", peer);
            int hrSend = client.SendText(ServerAddr, Topic, "Hello over a named pipe, through COM from C#!");
            Harness.Log("CLIENT", "SendText  : {0}", Hr.Name(hrSend));
        });
        client.OnPeerDown(peer => Harness.Log("CLIENT", "peer down : {0}", peer));
        client.OnError   (what => Harness.Log("CLIENT", "error     : {0}", what));

        hr = client.Connect(ServerAddr, Endpoint.Pipe(PipeName));
        if (Hr.Failed(hr)) return Harness.SetupFailure("Connect(pipe)", hr);
        Harness.Log("CLIENT", "opening the pipe (retries until it exists)");

        Harness.Log("MAIN", "waiting up to 10s for connect + delivery (pumping)...");
        bool ok = gDone.Wait(10000);

        Harness.Log("MAIN", "shutdown begin");
        return Harness.Verdict(ok,
                               "server received the client's message over the pipe",
                               "no message delivered (handshake did not complete)");
    }
}

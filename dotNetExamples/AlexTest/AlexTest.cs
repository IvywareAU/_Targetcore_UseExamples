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
// AlexTest.cs  (C# -- TargetCom)
//
// The original demo app: two PROCESSES, TCP, one message -- over COM, from C#.
//
//   AlexTestNet                       -- server mode (listens on port 7777)
//   AlexTestNet send [ip] [message]   -- client mode (connects, sends once)
//
// Two processes means two separate CoCreateInstance calls, each getting its own
// in-proc TargetCom, its own facade network and its own kernel. Nothing is
// shared but the socket -- which is the point of the exercise. Note that the two
// processes here are managed and the DLL between them is not: the CLR is loaded
// twice, MSCS is loaded twice, and neither knows about the other.
//
// Same deliberate change as the two rewrites before it: the original server
// blocked on getchar(), which cannot be run unattended (redirected stdin returns
// EOF immediately and the "server" exits before the client arrives). This one
// waits on the message, and additionally on a keypress ONLY when stdin is a real
// console. It still stops on Enter when run by hand.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : server -- received the client's message
//                 client -- peer came up and the message was sent
//   3 = TIMEOUT : the awaited event did not happen in time
//   1 = SETUP   : the COM server is not registered, or the hub could not be armed

using System;

using Com;
using TargetCom;

static class AlexTest
{
    const ushort TestPort   = 7777;
    const string ServerAddr = "AlexTest.Server";
    const string ClientAddr = "AlexTest.Client";
    const string Topic      = "chat";

    [STAThread]
    static int Main (string[] args)
    {
        Harness.InitConsole();

        bool   server = true;
        string ip     = "127.0.0.1";
        string msg    = "Hello from AlexTest client, through COM from C#!";

        if (args.Length >= 1 && string.Equals(args[0], "send", StringComparison.OrdinalIgnoreCase))
        {
            server = false;
            if (args.Length >= 2) ip  = args[1];
            if (args.Length >= 3) msg = args[2];
        }

        Harness.Print("=== AlexTest (C#) - P2P messaging demo ===");
        Harness.Print("Mode  : {0}", server ? "SERVER  (listen on port 7777)"
                                            : "CLIENT  (send one message)");
        if (!server) Harness.Print("Target: {0}:{1}", ip, TestPort);
        Harness.Print("");

        if (!Apartment.IsSta) return Harness.ExitSetup;

        int hr;
        using var net = Network.Create(out hr);
        if (net == null) return Harness.SetupFailure("CoCreateInstance(TargetCom.P2PNetwork)", hr);

        var gDone = new Gate();

        using var hub = net.CreateHub(server ? ServerAddr : ClientAddr, out hr);
        if (hub == null) return Harness.SetupFailure("CreateHub", hr);

        hub.OnPeerDown(peer => Harness.Log("HUB", "peer down : {0}", peer));
        hub.OnError   (what => Harness.Log("HUB", "error     : {0}", what));

        if (server)
        {
            hub.OnMessage(m =>
            {
                Harness.LogMessage("SERVER", m.Broadcast ? "broadcast" : "message", m.Source, m.Text);
                gDone.Open();
            });
            hub.OnPeerUp(peer => Harness.Log("SERVER", "peer up   : {0}", peer));

            hr = hub.Listen(ClientAddr, Endpoint.TcpListen(TestPort));
            if (Hr.Failed(hr)) return Harness.SetupFailure("Listen", hr);

            bool consoleIn = !Console.IsInputRedirected;

            Harness.Log("SERVER", "listening on port {0} - waiting up to 30s{1}",
                        TestPort,
                        consoleIn ? " (Enter to stop)" : " (stdin not a console: timeout only)");

            // Pump while waiting, and poll the console if there is one. The pump
            // is kept for the reason ComHarness.cs gives -- an agile sink does
            // not need it, but a client that stops pumping has stopped being a
            // correct STA and will break the moment anything non-agile appears.
            var sw = System.Diagnostics.Stopwatch.StartNew();
            bool ok = false;
            for (;;)
            {
                Pump.Once();
                if (gDone.IsOpen) { ok = true; break; }
                if (consoleIn && Console.KeyAvailable)
                {
                    Harness.Log("SERVER", "stopped from the console before any message arrived");
                    break;
                }
                if (sw.ElapsedMilliseconds > 30000) break;
                System.Threading.Thread.Sleep(5);
            }

            Harness.Log("MAIN", "shutdown begin");
            return Harness.Verdict(ok, "server received the client's message", "no message arrived");
        }
        else
        {
            hub.OnPeerUp(peer =>
            {
                Harness.Log("CLIENT", "peer up   : {0} - connection ready", peer);
                int h = hub.SendText(ServerAddr, Topic, msg);
                Harness.Log("CLIENT", "SendText  : {0}", Hr.Name(h));
                if (!Hr.Failed(h)) gDone.Open();
            });

            hr = hub.Connect(ServerAddr, Endpoint.TcpDial(ip, TestPort));
            if (Hr.Failed(hr)) return Harness.SetupFailure("Connect", hr);
            Harness.Log("CLIENT", "connecting to {0}:{1} (retries until answered)", ip, TestPort);

            bool ok = gDone.Wait(10000);
            if (ok) Pump.For(500);          // let the message reach the wire

            Harness.Log("MAIN", "shutdown begin");
            return Harness.Verdict(ok, "handshake completed and the message was sent",
                                       "the server never came up");
        }
    }
}

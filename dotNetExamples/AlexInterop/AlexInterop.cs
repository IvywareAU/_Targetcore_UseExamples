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
// AlexInterop.cs  (C# -- TargetCom)
//
// TWO-PROCESS loopback-TCP probe with a strict exit-code contract, over COM from
// managed code.
//
//   AlexInteropNet server [port]              -- listen, wait for the message
//   AlexInteropNet send   [ip] [port] [text]  -- connect, send one message
//
// WHAT IS LOST HERE -- worse than in either rewrite before it, and worth being
// blunt about. The original (DirectExamples\AlexInterop) exists to be
// PORTABLE: it is the Linux port's Phase-3 exit criterion, "AlexTest green
// Linux<->Linux", written to avoid every Win32-ism so the same source builds
// against the io_uring shim with g++.
//
// The facade rewrite already forfeited that by depending on a Windows MFC DLL.
// The COM rewrite forfeited it twice over: COM itself, the registry, apartments
// and BSTR/SAFEARRAY have no Linux counterpart. This one forfeits it a third
// time, and the third is the most complete: the whole binding mechanism is the
// Windows registry plus a CLR callable wrapper. There is no port of this file;
// there is only a rewrite. **If you are working the Linux port, use the
// original.**
//
// The interesting property it DOES have: two processes, two CLRs, two
// independent in-proc COM servers, two kernels, one socket between them.
//
// Verdict by EXIT CODE (both sides):
//   0 = SUCCESS  3 = TIMEOUT  1 = SETUP

using System;
using System.Diagnostics;

using Com;
using TargetCom;

static class AlexInterop
{
    const string ServerAddr = "AlexTest.Server";
    const string ClientAddr = "AlexTest.Client";
    const string Topic      = "chat";

    static void Usage ()
    {
        Harness.Print("usage:\n  AlexInteropNet server [port]\n  AlexInteropNet send   [ip] [port] [text]");
    }

    // This is the one harness whose port comes from argv, so it composes the
    // endpoint from an int rather than going through Com.Endpoint and its
    // ushort. The distinction is not pedantry: "70000" cast to ushort is 4464,
    // a perfectly valid port nobody asked for, whereas the whole number handed
    // to the parser comes back P2PF_E_ENDPOINT. The COM layer used to
    // range-check its LONG parameter by hand for exactly this reason; the
    // endpoint grammar now does it for every transport at once.
    static string TcpListenArg (int port)             { return "tcp://:" + port; }
    static string TcpDialArg (string host, int port)  { return "tcp://" + host + ":" + port; }

    [STAThread]
    static int Main (string[] args)
    {
        Harness.InitConsole();

        bool   server = true;
        string ip     = "127.0.0.1";
        int    port   = 7811;
        string msg    = "Hello from AlexTest client (two processes, COM, C#)!";

        if (args.Length >= 1)
        {
            if (string.Equals(args[0], "send", StringComparison.OrdinalIgnoreCase))
            {
                server = false;
                if (args.Length >= 2) ip   = args[1];
                if (args.Length >= 3) int.TryParse(args[2], out port);
                if (args.Length >= 4) msg  = args[3];
            }
            else if (string.Equals(args[0], "server", StringComparison.OrdinalIgnoreCase))
            {
                if (args.Length >= 2) int.TryParse(args[1], out port);
            }
            else { Usage(); return Harness.ExitSetup; }
        }

        Harness.Print("=== AlexInterop (C#, two-process) - {0} ===", server ? "SERVER" : "CLIENT");
        Harness.Print("Port : {0}  IP : {1}  pid : {2}\n",
                      port, server ? "127.0.0.1(listen)" : ip, Process.GetCurrentProcess().Id);

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

            hr = hub.Listen(ClientAddr, TcpListenArg(port));
            if (Hr.Failed(hr)) return Harness.SetupFailure("Listen", hr);
            Harness.Log("SERVER", "listening on port {0}, waiting up to 15s (pumping)...", port);

            bool ok = gDone.Wait(15000);
            Harness.Log("MAIN", "shutdown begin");
            return Harness.Verdict(ok,
                                   "received the client's message across the process boundary",
                                   "no message arrived");
        }
        else
        {
            hub.OnPeerUp(peer =>
            {
                Harness.Log("CLIENT", "peer up   : {0} - TCP connection ready", peer);
                int h = hub.SendText(ServerAddr, Topic, msg);
                Harness.Log("CLIENT", "SendText  : {0}", Hr.Name(h));
                if (!Hr.Failed(h)) gDone.Open();
            });

            hr = hub.Connect(ServerAddr, TcpDialArg(ip, port));
            if (Hr.Failed(hr)) return Harness.SetupFailure("Connect", hr);
            Harness.Log("CLIENT", "dialling {0}:{1}, waiting up to 10s (pumping)...", ip, port);

            bool ok = gDone.Wait(10000);
            if (ok) Pump.For(1000);

            Harness.Log("MAIN", "shutdown begin");
            return Harness.Verdict(ok, "handshake completed and the message was sent",
                                       "the server never came up");
        }
    }
}

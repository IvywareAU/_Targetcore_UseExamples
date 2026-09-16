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
// LocalInMemoryTest.cs  (C# -- TargetCom)
//
// TWO HUBS, ONE PROCESS, NO WIRE -- bidirectional delivery with no socket, no
// pipe and no OS handle, driven through COM from managed code.
//
// The same caveat as the other rewrites, one layer further out: the original
// used the kernel's pump-injection call PostP2Pmsg(msg, targetHub.GetHubID()),
// which bypasses the connection layer entirely. Neither the facade nor the COM
// layer exposes that -- the model is that hubs talk over connections -- so this
// reaches the same end state over the Dmx transport and pays one login
// handshake for it. If you need pump injection, use Targetcore directly.
//
// The ordering rule found while writing the Light tree applies here too, and it
// is worth restating because a managed client is furthest of all from the
// evidence:
//
//     OnPeerUp on the LISTENING side can fire BEFORE the dialling side has
//     finished logging in. Sending from it races the handshake and the far end
//     kills the connection with "Application message received before login".
//
// So HubB (which dialled) speaks first and HubA answers on receipt.
//
// Note also what the agile-CCW finding does to this file specifically: HubA's
// reply is sent from INSIDE HubA's dispatch thread, while HubB's send runs on
// HubB's. In the C++ tree both ran on the STA main thread, one after the other.
// Nothing here needs the difference, but it is why the two Gates are the only
// shared state.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : BOTH hubs received the message addressed to them.
//   3 = TIMEOUT : one or both deliveries did not complete in time.
//   1 = SETUP   : the COM server is not registered, or a hub could not be armed.

using System;

using Com;
using TargetCom;

static class LocalInMemoryTest
{
    const string ServiceName = "P2PlocalMeshNet";
    const string AddrHubA    = "LocalMesh.HubA";
    const string AddrHubB    = "LocalMesh.HubB";
    const string Topic       = "local";

    [STAThread]
    static int Main ()
    {
        Harness.InitConsole();
        Harness.Print("=== LocalInMemoryTest (C#) - two hubs, one process, no wire ===");
        Harness.Print("Delivery mechanism: Dmx connection (in-address-space, no OS handle)\n");

        if (!Apartment.IsSta) return Harness.ExitSetup;

        int hr;
        using var net = Network.Create(out hr);
        if (net == null) return Harness.SetupFailure("CoCreateInstance(TargetCom.P2PNetwork)", hr);

        var gRecvA = new Gate();
        var gRecvB = new Gate();

        // ---- Hub A: the LISTENING side. Answers, never opens. ----------------
        using var hubA = net.CreateHub(AddrHubA, out hr);
        if (hubA == null) return Harness.SetupFailure("CreateHub(HubA)", hr);

        hubA.OnTopic(Topic, m =>
        {
            Harness.LogMessage("HubA", "message", m.Source, m.Text);
            gRecvA.Open();

            int h = hubA.SendText(AddrHubB, Topic, "Hello HubB - delivered in memory, no wire!");
            Harness.Log("HubA", "A -> B  : {0}", Hr.Name(h));
        });
        hubA.OnPeerUp(peer => Harness.Log("HubA", "peer up : {0}", peer));
        hubA.OnError (what => Harness.Log("HubA", "error : {0}", what));

        hr = hubA.Listen(AddrHubB, Endpoint.Dmx(ServiceName));
        if (Hr.Failed(hr)) return Harness.SetupFailure("Listen(dmx)", hr);

        // ---- Hub B: the DIALLING side. Its peer-up means login-ack. ----------
        using var hubB = net.CreateHub(AddrHubB, out hr);
        if (hubB == null) return Harness.SetupFailure("CreateHub(HubB)", hr);

        hubB.OnTopic(Topic, m =>
        {
            Harness.LogMessage("HubB", "message", m.Source, m.Text);
            gRecvB.Open();
        });
        hubB.OnPeerUp(peer =>
        {
            Harness.Log("HubB", "peer up : {0} - in-process link ready", peer);
            int h = hubB.SendText(AddrHubA, Topic, "Hello HubA - same process, straight to your pump!");
            Harness.Log("HubB", "B -> A  : {0}", Hr.Name(h));
        });
        hubB.OnError(what => Harness.Log("HubB", "error : {0}", what));

        hr = hubB.Connect(AddrHubA, Endpoint.Dmx(ServiceName));
        if (Hr.Failed(hr)) return Harness.SetupFailure("Connect(dmx)", hr);
        Harness.Log("MAIN", "both hubs up, linked in memory");

        Harness.Log("MAIN", "waiting up to 10s for both in-memory deliveries (pumping)...");
        bool ok = Gate.WaitAll(10000, gRecvA, gRecvB);

        Harness.Log("MAIN", "shutdown begin");
        return Harness.Verdict(ok,
                               "both hubs received their in-memory message",
                               "one or both deliveries did not complete");
    }
}

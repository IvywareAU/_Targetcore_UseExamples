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
// Com232MeshTest.cs  (C# -- TargetCom)
//
// SINGLE-PROCESS, TWO-HUB RS-232 serial connection probe, over COM from managed
// code.
//
// PREREQUISITE (unchanged from all three other trees): a com0com virtual
// null-modem pair COM5<->COM6
//     setupc install PortName=COM5 PortName=COM6
// Without it this reports SETUP and exits 1.
//
// The port used to cross as a LONG -- a plain `int` here -- so the COM layer
// could range-check it before the facade saw a truncated `short`. Since ABI 4
// there is no numeric parameter left: the port is text inside "serial://COM5"
// and the check moved into the endpoint parser, which every one of the four
// trees now shares instead of just the two COM ones.
//
// That move takes the *shape* of the error with it, and this harness is where
// that is visible. See the two-part section near the bottom.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : the server hub received the client's message over the wire.
//   3 = TIMEOUT : handshake or delivery did not complete in time.
//   1 = SETUP   : no com0com pair, or the COM server is not registered.

using System;
using System.Runtime.InteropServices;

using Com;
using TargetCom;

static class Com232MeshTest
{
    const int    ServerComPort = 5;
    const int    ClientComPort = 6;
    const string ServerAddr    = "Com232Mesh.Server";
    const string ClientAddr    = "Com232Mesh.Client";
    const string Topic         = "mesh";

    [STAThread]
    static int Main ()
    {
        Harness.InitConsole();
        Harness.Print("=== Com232MeshTest (C#) - two hubs, one process, RS-232 ===");
        Harness.Print("Server : COM{0}   Client : COM{1}   (com0com null-modem pair required)\n",
                      ServerComPort, ClientComPort);

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

        // Armed first: a serial:// dial does not retry.
        hr = server.Listen(ClientAddr, Endpoint.Serial(ServerComPort));
        if (Hr.Failed(hr))
        {
            Harness.Log("SERVER", "SETUP: Listen(serial://COM{0}) -> {1}", ServerComPort, Hr.Name(hr));
            Harness.Print("\nNo com0com pair on COM{0}/COM{1}? Install one with:\n"
                        + "    setupc install PortName=COM{0} PortName=COM{1}",
                          ServerComPort, ClientComPort);
            return Harness.ExitSetup;
        }
        Harness.Log("SERVER", "COM{0} armed for '{1}'", ServerComPort, ClientAddr);

        using var client = net.CreateHub(ClientAddr, out hr);
        if (client == null) return Harness.SetupFailure("CreateHub(client)", hr);

        client.OnPeerUp(peer =>
        {
            Harness.Log("CLIENT", "peer up   : {0} - serial link ready", peer);
            int hrSend = client.SendText(ServerAddr, Topic, "Hello over RS-232, through COM from C#!");
            Harness.Log("CLIENT", "SendText  : {0}", Hr.Name(hrSend));
        });
        client.OnPeerDown(peer => Harness.Log("CLIENT", "peer down : {0}", peer));
        client.OnError   (what => Harness.Log("CLIENT", "error     : {0}", what));

        hr = client.Connect(ServerAddr, Endpoint.Serial(ClientComPort));
        if (Hr.Failed(hr))
        {
            Harness.Log("CLIENT", "SETUP: Connect(serial://COM{0}) -> {1}", ClientComPort, Hr.Name(hr));
            return Harness.ExitSetup;
        }
        Harness.Log("CLIENT", "dialled from COM{0}", ClientComPort);

        // ---- PART 1: the range check, now in the endpoint parser -------------
        //
        // "serial://COM0" is a legal-looking string with an illegal port in it.
        // The parser insists the whole segment be numeric AND in range, so this
        // is rejected for the same reason "serial://COMx" would be.
        //
        // P2PF_E_ENDPOINT is a custom FACILITY_ITF code, so the CLR leaves it
        // alone and it arrives as an ordinary COMException -- which Com.Hub
        // turns back into an int. Contrast PART 2.
        int hrBad = server.Listen("Com232Mesh.Nope", Endpoint.Serial(0));
        Harness.Log("MAIN", "Listen(serial://COM0) -> {0} (range-checked in the parser)", Hr.Name(hrBad));

        // ---- PART 2: where the CLR still reshapes the error contract ---------
        //
        // This demonstration used to live on ListenSerial(port 0). The COM
        // layer's private Port() helper went away with its numeric parameters,
        // so its E_INVALIDARG went with it -- and CreateHub's empty-address
        // check is now the layer's ONLY remaining argument validation.
        //
        // The lesson is unchanged and still worth the lines: E_INVALIDARG is in
        // the CLR's HRESULT-to-exception mapping table, so the call site does
        // NOT get a COMException. It gets an ArgumentException, with the HRESULT
        // only recoverable via Marshal.GetHRForException. A C# client that
        // wrapped this API in `catch (COMException)` would sail past it and
        // crash. That is a property of the CLR, not of this interface, and no
        // amount of IDL tidying makes it go away.
        int hrEmpty = Hr.S_OK;
        try
        {
            net.Raw.CreateHub("");
            Harness.Log("MAIN", "CreateHub(\"\") was ACCEPTED - unexpected");
        }
        catch (COMException e)
        {
            hrEmpty = e.HResult;
            Harness.Log("MAIN", "CreateHub(\"\") -> COMException 0x{0:X8}", e.HResult);
        }
        catch (ArgumentException e)
        {
            hrEmpty = Marshal.GetHRForException(e);
            Harness.Log("MAIN", "CreateHub(\"\") -> {0} 0x{1:X8}  <- NOT a COMException; "
                              + "the CLR rewrote E_INVALIDARG", e.GetType().Name, hrEmpty);
        }

        Harness.Log("MAIN", "waiting up to 15s for login + delivery (pumping)...");
        bool ok = gDone.Wait(15000)
               && hrBad   == Hr.P2PF_E_ENDPOINT
               && hrEmpty == Hr.E_INVALIDARG;

        Harness.Log("MAIN", "shutdown begin");
        return Harness.Verdict(ok,
                               "server received the client's message over RS-232",
                               "no message delivered (handshake did not complete)");
    }
}

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
// AlexInterop.java  (Java -- Panama FFI over TargetFacade)
//
// TWO-PROCESS loopback-TCP probe with a strict exit-code contract, meant to be
// driven by an orchestrator that launches both sides and asserts BOTH exit 0.
//
//   java mscs.examples.AlexInterop server [port]
//   java mscs.examples.AlexInterop send   [ip] [port] [text]
//
// WHAT IS LOST HERE, STATED PLAINLY -- and it is lost twice over in this tree.
//
// The original (DirectExamples\AlexInterop, alex_test.cpp) exists to be
// PORTABLE: it is the Linux port's Phase-3 exit criterion, "AlexTest green
// Linux<->Linux", and it avoids every Win32-ism so the same source compiles
// against the io_uring shim with g++.
//
// The Light rewrite forfeited that once, because TargetFacade is a Windows MFC
// DLL. This one forfeits it twice: the binding in mscs.p2pf is x64 Windows only
// by construction -- it assumes 8-byte pointers, MSVC vtable layout, and
// wchar_t == UTF-16. Java itself would have carried this harness anywhere; what
// pins it is the DLL underneath and the ABI assumptions above it.
//
// That is worth stating rather than hiding, because it inverts the usual reason
// to bind a native library from Java. The Java tree is the LEAST portable of the
// five, not the most. If you are working the Linux port, use the original.
//
// What it does still show is how small the two-process shape gets when the
// facade owns the lifecycle: no WSAStartup, no SpawnHub, no CloseHandle, and no
// assert hook, because there is no MFC assert of ours to trap.
//
// Verdict by EXIT CODE (both sides):
//   0 = SUCCESS : server -- received the client's message (proves the message
//                           crossed the process boundary)
//                 client -- peer came up and the message was sent
//   3 = TIMEOUT : the awaited event did not fire before the deadline
//   1 = SETUP   : startup / arming failure
package mscs.examples;

import mscs.harness.Harness;
import mscs.p2pf.Abi;
import mscs.p2pf.Hub;
import mscs.p2pf.Network;

public final class AlexInterop {

    private static final String SERVER_ADDR = "AlexTest.Server";
    private static final String CLIENT_ADDR = "AlexTest.Client";
    private static final String TOPIC       = "chat";

    private static void usage() {
        Harness.banner("usage:");
        Harness.banner("  java mscs.examples.AlexInterop server [port]");
        Harness.banner("  java mscs.examples.AlexInterop send   [ip] [port] [text]");
    }

    public static void main(String[] args) {
        boolean server = true;
        String  ip     = "127.0.0.1";
        int     port   = 7811;
        String  msg    = "Hello from AlexTest client (two processes)!";

        if (args.length >= 1) {
            if (args[0].equalsIgnoreCase("send")) {
                server = false;
                if (args.length >= 2) ip   = args[1];
                if (args.length >= 3) port = Integer.parseInt(args[2]);
                if (args.length >= 4) msg  = args[3];
            } else if (args[0].equalsIgnoreCase("server")) {
                if (args.length >= 2) port = Integer.parseInt(args[1]);
            } else {
                usage();
                System.exit(Harness.EXIT_SETUP);
            }
        }

        final boolean isServer = server;
        final String  theIp    = ip;
        final int     thePort  = port;
        final String  theMsg   = msg;

        Harness.run("AlexInterop", () -> {
            Harness.banner("=== AlexInterop (Java, two-process) - "
                           + (isServer ? "SERVER" : "CLIENT") + " ===");
            Harness.banner("Port : " + thePort
                           + "  IP : " + (isServer ? "127.0.0.1(listen)" : theIp)
                           + "  pid : " + ProcessHandle.current().pid());
            Harness.banner("");

            Harness.Gate done = new Harness.Gate();

            try (Network net = Network.open();
                 Hub hub = net.createHub(isServer ? SERVER_ADDR : CLIENT_ADDR)) {

                hub.onPeerDown(p -> Harness.log("HUB", "peer down : %s", p));
                hub.onError   (w -> Harness.log("HUB", "error     : %s", w));

                if (isServer) {
                    hub.onMessage(m -> {
                        Harness.logMessage("SERVER", m.broadcast ? "broadcast" : "message",
                                           m.source, m.text());
                        done.open();
                    });
                    hub.onPeerUp(p -> Harness.log("SERVER", "peer up   : %s", p));

                    int hr = hub.listen(CLIENT_ADDR, Abi.tcpListen(thePort));
                    if (Abi.failed(hr)) {
                        Harness.log("SERVER", "FATAL: listen failed (%s)", Abi.name(hr));
                        return Harness.EXIT_SETUP;
                    }
                    Harness.log("SERVER", "listening on port %d, waiting up to 15s...", thePort);

                    boolean ok = done.await(15_000);
                    Harness.log("MAIN", "shutdown begin");
                    return Harness.verdict(ok,
                            "received the client's message across the process boundary",
                            "no message arrived");
                } else {
                    hub.onPeerUp(p -> {
                        Harness.log("CLIENT", "peer up   : %s - TCP connection ready", p);
                        int sent = hub.sendText(SERVER_ADDR, TOPIC, theMsg);
                        Harness.log("CLIENT", "sendText  : %s", Abi.name(sent));
                        if (!Abi.failed(sent)) done.open();
                    });

                    int hr = hub.connect(SERVER_ADDR, Abi.tcpDial(theIp, thePort));
                    if (Abi.failed(hr)) {
                        Harness.log("CLIENT", "FATAL: connect failed (%s)", Abi.name(hr));
                        return Harness.EXIT_SETUP;
                    }
                    Harness.log("CLIENT", "dialling %s:%d, waiting up to 10s...", theIp, thePort);

                    boolean ok = done.await(10_000);

                    // Give the posted message time to reach the wire before teardown.
                    if (ok) Harness.sleep(1_000);

                    Harness.log("MAIN", "shutdown begin");
                    return Harness.verdict(ok, "handshake completed and the message was sent",
                                               "the server never came up");
                }
            }
        });
    }
}

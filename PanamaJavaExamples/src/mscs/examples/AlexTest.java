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
// AlexTest.java  (Java -- Panama FFI over TargetFacade)
//
// The original demo app: two PROCESSES, TCP, one message. It is the ancestor
// every other harness in this tree descends from.
//
//   java mscs.examples.AlexTest                      -- server (listens on 7777)
//   java mscs.examples.AlexTest send [ip] [message]  -- client (connects, sends once)
//
// Start the server first, then the client -- though with the facade that is
// advice rather than a requirement, because a facade dial retries until the far
// side answers. The original's client got exactly one attempt.
//
// ONE DELIBERATE CHANGE FROM THE ORIGINAL, inherited from the Light rewrite. The
// original server blocked on getchar(), which makes it impossible to run
// unattended: with stdin redirected, getchar() returns EOF immediately and the
// "server" exits before the client can reach it. This server waits for the
// message with a timeout and reports the outcome as an exit code, so run_all.ps1
// can drive both processes.
//
// The Light version guarded that with GetConsoleMode, waiting on the stdin
// HANDLE only when it is a real console. There is no portable Java equivalent --
// System.console() is null under a redirected stdin, which is the same signal,
// but you cannot then WAIT on the handle alongside the gate. So this version
// drops the Enter-to-stop leg entirely and waits on the timeout alone. It is a
// smaller behaviour than the C++ trees have, and it is the honest one: an
// unattended run behaves identically, and an attended one just waits.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : server -- received the client's message
//                 client -- peer came up and the message was sent
//   3 = TIMEOUT : the awaited event did not happen in time
//   1 = SETUP   : the network or the hub could not be created / armed
package mscs.examples;

import mscs.harness.Harness;
import mscs.p2pf.Abi;
import mscs.p2pf.Hub;
import mscs.p2pf.Network;

public final class AlexTest {

    private static final int    PORT        = 7777;
    private static final String SERVER_ADDR = "AlexTest.Server";
    private static final String CLIENT_ADDR = "AlexTest.Client";
    private static final String TOPIC       = "chat";

    public static void main(String[] args) {
        boolean server = true;
        String  ip     = "127.0.0.1";
        String  msg    = "Hello from AlexTest client!";

        if (args.length >= 1 && args[0].equalsIgnoreCase("send")) {
            server = false;
            if (args.length >= 2) ip  = args[1];
            if (args.length >= 3) msg = args[2];
        }

        final boolean isServer = server;
        final String  theIp    = ip;
        final String  theMsg   = msg;

        Harness.run("AlexTest", () -> {
            Harness.banner("=== AlexTest (Java) - P2P messaging demo ===");
            Harness.banner("Mode  : " + (isServer ? "SERVER  (listen on port " + PORT + ")"
                                                  : "CLIENT  (send one message)"));
            if (!isServer) Harness.banner("Target: " + theIp + ":" + PORT);
            Harness.banner("");

            Harness.Gate done = new Harness.Gate();

            // Startup and shutdown of the whole kernel, in one object, on every
            // exit path -- including the exceptional ones. The original repeated
            // CleanupP2Pmsg + WSACleanup + CloseHandle at six different returns.
            try (Network net = Network.open()) {
                Harness.log("MAIN", "facade : %s  pid : %d",
                            net.versionString(), ProcessHandle.current().pid());

                try (Hub hub = net.createHub(isServer ? SERVER_ADDR : CLIENT_ADDR)) {
                    hub.onPeerDown(p -> Harness.log("HUB", "peer down : %s", p));
                    hub.onError   (w -> Harness.log("HUB", "error     : %s", w));

                    if (isServer) {
                        hub.onMessage(m -> {
                            Harness.logMessage("SERVER", m.broadcast ? "broadcast" : "message",
                                               m.source, m.text());
                            done.open();
                        });
                        hub.onPeerUp(p -> Harness.log("SERVER", "peer up   : %s", p));

                        int hr = hub.listen(CLIENT_ADDR, Abi.tcpListen(PORT));
                        if (Abi.failed(hr)) {
                            Harness.log("SERVER", "FATAL: listen failed (%s)", Abi.name(hr));
                            return Harness.EXIT_SETUP;
                        }
                        Harness.log("SERVER", "listening on port %d - waiting up to 30s", PORT);

                        boolean ok = done.await(30_000);

                        Harness.log("MAIN", "shutdown begin");
                        return Harness.verdict(ok, "server received the client's message",
                                                   "no message arrived");
                    } else {
                        hub.onPeerUp(p -> {
                            Harness.log("CLIENT", "peer up   : %s - connection ready", p);
                            int sent = hub.sendText(SERVER_ADDR, TOPIC, theMsg);
                            Harness.log("CLIENT", "sendText  : %s", Abi.name(sent));
                            if (!Abi.failed(sent)) done.open();
                        });

                        int hr = hub.connect(SERVER_ADDR, Abi.tcpDial(theIp, PORT));
                        if (Abi.failed(hr)) {
                            Harness.log("CLIENT", "FATAL: connect failed (%s)", Abi.name(hr));
                            return Harness.EXIT_SETUP;
                        }
                        Harness.log("CLIENT", "connecting to %s:%d (retries until answered)",
                                    theIp, PORT);

                        boolean ok = done.await(10_000);

                        // Let the posted message reach the wire before the hub
                        // goes down. sendText queues; Close does not flush.
                        if (ok) Harness.sleep(500);

                        Harness.log("MAIN", "shutdown begin");
                        return Harness.verdict(ok, "handshake completed and the message was sent",
                                                   "the server never came up");
                    }
                }
            }
        });
    }
}

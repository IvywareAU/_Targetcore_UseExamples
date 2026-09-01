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
// WsaMeshTest.java  (Java -- Panama FFI over TargetFacade)
//
// SINGLE-PROCESS, TWO-HUB LOOPBACK-TCP connectivity probe.
//
// Same question as DirectExamples\WsaMeshTest: can two hubs in ONE process
// connect over loopback TCP and complete the login handshake? Same verdict by
// exit code. What changed is everything below the question:
//
//   original (286 lines, C++/MFC)          java (this file)
//   ---------------------------------------------------------------------
//   CWinApp theApp + MFC stdafx            (gone)
//   StartupP2Pmsg + WSAStartup             Network.open()
//   P2PeerHub subclass + SpawnHub()        net.createHub(addr)
//   7 On_Con* trace overrides              hub.onPeerUp(...)
//   On_P2PeerBCast override                hub.onTopic("mesh", ...)
//   ServiceFactory + PostP2PeerCon         hub.listen(peer, "tcp://:port")
//   ClientFactory + PostP2PeerCon          hub.connect(peer, "tcp://ip:port")
//   new P2PeerMsg32(...) + PostP2PeerMsg   hub.sendText(dest, topic, text)
//   _CrtSetReportHook assert trap          (unreachable -- no MFC of ours here)
//   CloseHub/Wait/CloseHandle/Cleanup      try-with-resources, in order
//
// This is also the tree's load-bearing harness in one other sense: it is the
// first thing that proves an MFC extension DLL initialises inside a plain JVM
// process, with no CWinApp anywhere. See README.md.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : the server hub received the client's message over loopback TCP.
//   3 = TIMEOUT : handshake or delivery did not complete in time.
//   1 = SETUP   : the network or a hub could not be created / armed.
package mscs.examples;

import mscs.harness.Harness;
import mscs.p2pf.Abi;
import mscs.p2pf.Hub;
import mscs.p2pf.Network;

public final class WsaMeshTest {

    private static final int    PORT        = 7801;
    private static final String SERVER_ADDR = "WsaMesh.Server";
    private static final String CLIENT_ADDR = "WsaMesh.Client";
    private static final String TOPIC       = "mesh";

    public static void main(String[] args) {
        Harness.run("WsaMeshTest", () -> {
            Harness.banner("=== WsaMeshTest (Java) - two hubs, one process, loopback TCP ===");
            Harness.banner("Port : " + PORT + " (127.0.0.1)");
            Harness.banner("");

            Harness.Gate done = new Harness.Gate();   // opened when the SERVER receives

            // ONE object replaces StartupP2Pmsg(16) + WSAStartup(2.2) and their
            // matching teardown calls on every exit path.
            try (Network net = Network.open()) {
                Harness.log("MAIN", "facade : %s", net.versionString());

                // ---- Hub A: SERVER ------------------------------------------
                // createHub() constructs the hub AND spawns its pump thread;
                // there is no separate SpawnHub()/CloseHub() to get wrong.
                try (Hub server = net.createHub(SERVER_ADDR);
                     Hub client = net.createHub(CLIENT_ADDR)) {

                    // This is the P2PeerMsg_MAP replacement. No macros, no subclass.
                    server.onTopic(TOPIC, m -> {
                        Harness.logMessage("SERVER", "message", m.source, m.text());
                        done.open();
                    });
                    server.onPeerUp  (p -> Harness.log("SERVER", "peer up   : %s", p));
                    server.onPeerDown(p -> Harness.log("SERVER", "peer down : %s", p));
                    server.onError   (w -> Harness.log("SERVER", "error     : %s", w));

                    int hr = server.listen(CLIENT_ADDR, Abi.tcpListen(PORT));
                    if (Abi.failed(hr)) {
                        Harness.log("SERVER", "FATAL: listen failed (%s)", Abi.name(hr));
                        return Harness.EXIT_SETUP;
                    }
                    Harness.log("SERVER", "listening for '%s' on port %d", CLIENT_ADDR, PORT);

                    // ---- Hub B: CLIENT --------------------------------------
                    // The handshake milestone the original watched for through
                    // On_ConLoginAck is just this callback.
                    client.onPeerUp(p -> {
                        Harness.log("CLIENT", "peer up   : %s - loopback TCP connection ready", p);
                        int sent = client.sendText(SERVER_ADDR, TOPIC,
                                                   "Hello over loopback TCP, in one process!");
                        Harness.log("CLIENT", "sendText  : %s", Abi.name(sent));
                    });
                    client.onPeerDown(p -> Harness.log("CLIENT", "peer down : %s", p));
                    client.onError   (w -> Harness.log("CLIENT", "error     : %s", w));

                    hr = client.connect(SERVER_ADDR, Abi.tcpDial("127.0.0.1", PORT));
                    if (Abi.failed(hr)) {
                        Harness.log("CLIENT", "FATAL: connect failed (%s)", Abi.name(hr));
                        return Harness.EXIT_SETUP;
                    }
                    Harness.log("CLIENT", "dialling 127.0.0.1:%d (retries until answered)", PORT);

                    // ---- Wait for the round trip ----------------------------
                    Harness.log("MAIN", "waiting up to 10s for connect + delivery...");
                    boolean ok = done.await(10_000);

                    Harness.log("MAIN", "shutdown begin");
                    return Harness.verdict(ok,
                            "server received the client's message over loopback TCP",
                            "no message delivered (handshake did not complete)");
                    // Hub.close() closes each pump; Network.close() shuts the
                    // kernel down. In that order, by try-with-resources.
                }
            }
        });
    }
}

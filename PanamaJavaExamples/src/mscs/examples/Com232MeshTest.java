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
// Com232MeshTest.java  (Java -- Panama FFI over TargetFacade)
//
// SINGLE-PROCESS, TWO-HUB RS-232 serial connection probe.
//
// Fourth instance of the same shape, fourth endpoint scheme: "serial://COMn".
// The two hubs sit on the two ends of a com0com virtual null-modem pair,
// exactly as in the original.
//
// Note that each side names its OWN port, which is why this is the one
// transport IP2PNetwork::Link cannot express: a null-modem link is two
// DIFFERENT local devices, each opened exclusively, and Link carries one
// endpoint for both sides.
//
// PREREQUISITE (unchanged from the original): a com0com pair COM5<->COM6
//   setupc install PortName=COM5 PortName=COM6
// Without it this harness reports SETUP and exits 1 -- it is the one example
// here that cannot run on a machine with no serial pair.
//
// Ordering: a serial:// dial does NOT retry (a missing COM port is a
// configuration fault, not a timing one), so the listening end is armed first --
// the same rule the original enforced with Sleep(1500), and for the same
// underlying reason: WaitCommEvent only reports events raised after
// SetCommMask, so login bytes arriving before the arm would go unnoticed.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : the server hub received the client's message over the wire.
//   3 = TIMEOUT : handshake or delivery did not complete in time.
//   1 = SETUP   : no com0com pair (or the ports are in use).
package mscs.examples;

import mscs.harness.Harness;
import mscs.p2pf.Abi;
import mscs.p2pf.Hub;
import mscs.p2pf.Network;

public final class Com232MeshTest {

    private static final int    SERVER_COM  = 5;    // server listens on COM5
    private static final int    CLIENT_COM  = 6;    // client dials from COM6
    private static final String SERVER_ADDR = "Com232Mesh.Server";
    private static final String CLIENT_ADDR = "Com232Mesh.Client";
    private static final String TOPIC       = "mesh";

    public static void main(String[] args) {
        Harness.run("Com232MeshTest", () -> {
            Harness.banner("=== Com232MeshTest (Java) - two hubs, one process, RS-232 ===");
            Harness.banner("Server : COM" + SERVER_COM + "   Client : COM" + CLIENT_COM
                           + "   (com0com null-modem pair required)");
            Harness.banner("");

            Harness.Gate done = new Harness.Gate();

            try (Network net = Network.open()) {
                Harness.log("MAIN", "facade : %s", net.versionString());

                try (Hub server = net.createHub(SERVER_ADDR);
                     Hub client = net.createHub(CLIENT_ADDR)) {

                    // ---- Hub A: SERVER -- armed FIRST -----------------------
                    server.onTopic(TOPIC, m -> {
                        Harness.logMessage("SERVER", "message", m.source, m.text());
                        done.open();
                    });
                    server.onPeerUp  (p -> Harness.log("SERVER", "peer up   : %s", p));
                    server.onPeerDown(p -> Harness.log("SERVER", "peer down : %s", p));
                    server.onError   (w -> Harness.log("SERVER", "error     : %s", w));

                    int hr = server.listen(CLIENT_ADDR, Abi.serial(SERVER_COM));
                    if (Abi.failed(hr)) {
                        Harness.log("SERVER", "SETUP: listen(serial://COM%d) failed (%s)",
                                    SERVER_COM, Abi.name(hr));
                        Harness.banner("");
                        Harness.banner("No com0com pair on COM" + SERVER_COM + "/COM" + CLIENT_COM
                                       + "? Install one with:");
                        Harness.banner("    setupc install PortName=COM" + SERVER_COM
                                       + " PortName=COM" + CLIENT_COM);
                        return Harness.EXIT_SETUP;
                    }
                    Harness.log("SERVER", "COM%d armed for '%s'", SERVER_COM, CLIENT_ADDR);

                    // ---- Hub B: CLIENT --------------------------------------
                    client.onPeerUp(p -> {
                        Harness.log("CLIENT", "peer up   : %s - serial link ready", p);
                        int sent = client.sendText(SERVER_ADDR, TOPIC,
                                "Hello over RS-232, one hub at each end of the null modem!");
                        Harness.log("CLIENT", "sendText  : %s", Abi.name(sent));
                    });
                    client.onPeerDown(p -> Harness.log("CLIENT", "peer down : %s", p));
                    client.onError   (w -> Harness.log("CLIENT", "error     : %s", w));

                    hr = client.connect(SERVER_ADDR, Abi.serial(CLIENT_COM));
                    if (Abi.failed(hr)) {
                        Harness.log("CLIENT", "SETUP: connect(serial://COM%d) failed (%s)",
                                    CLIENT_COM, Abi.name(hr));
                        return Harness.EXIT_SETUP;
                    }
                    Harness.log("CLIENT", "dialled from COM%d", CLIENT_COM);

                    Harness.log("MAIN", "waiting up to 15s for login + delivery over the wire...");
                    boolean ok = done.await(15_000);

                    Harness.log("MAIN", "shutdown begin");
                    return Harness.verdict(ok,
                            "server received the client's message over RS-232",
                            "no message delivered (handshake did not complete)");
                }
            }
        });
    }
}

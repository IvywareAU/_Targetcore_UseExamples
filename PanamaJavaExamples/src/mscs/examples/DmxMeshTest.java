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
// DmxMeshTest.java  (Java -- Panama FFI over TargetFacade)
//
// SINGLE-PROCESS, TWO-HUB in-process CONNECTION probe (Dmx transport).
//
// Third instance of the same shape, third endpoint scheme: "dmx://service".
// Dmx is the in-address-space rendezvous transport -- a real connection with a
// real login handshake, but no OS handle at all; endpoints are matched by a
// shared SERVICE NAME rather than a port or a pipe path.
//
// ORDERING MATTERS HERE, and this is the one place the facade does not paper
// over it. Facade tcp:// and pipe:// dials retry, so arming order is irrelevant
// for those. A dmx:// dial does NOT retry: a missing in-process service is a
// configuration fault, not a timing one, so the listener must be armed first.
// Note that the retry policy follows the TRANSPORT, not the verb, which is why
// it survived the eight arming verbs collapsing onto two.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : the server hub received the client's message over Dmx.
//   3 = TIMEOUT : handshake or delivery did not complete in time.
//   1 = SETUP   : the network or a hub could not be created / armed.
package mscs.examples;

import mscs.harness.Harness;
import mscs.p2pf.Abi;
import mscs.p2pf.Hub;
import mscs.p2pf.Network;

public final class DmxMeshTest {

    private static final String SERVICE_NAME = "P2PdmxProbeJava";
    private static final String SERVER_ADDR  = "DmxMesh.Server";
    private static final String CLIENT_ADDR  = "DmxMesh.Client";
    private static final String TOPIC        = "mesh";

    public static void main(String[] args) {
        Harness.run("DmxMeshTest", () -> {
            Harness.banner("=== DmxMeshTest (Java) - two hubs, one process, Dmx transport ===");
            Harness.banner("Service : " + SERVICE_NAME);
            Harness.banner("");

            Harness.Gate done = new Harness.Gate();

            try (Network net = Network.open()) {
                Harness.log("MAIN", "facade : %s", net.versionString());

                try (Hub server = net.createHub(SERVER_ADDR);
                     Hub client = net.createHub(CLIENT_ADDR)) {

                    // ---- Hub A: SERVER -- armed FIRST (a dmx:// dial does not retry) ----
                    server.onTopic(TOPIC, m -> {
                        Harness.logMessage("SERVER", "message", m.source, m.text());
                        done.open();
                    });
                    server.onPeerUp  (p -> Harness.log("SERVER", "peer up   : %s", p));
                    server.onPeerDown(p -> Harness.log("SERVER", "peer down : %s", p));
                    server.onError   (w -> Harness.log("SERVER", "error     : %s", w));

                    int hr = server.listen(CLIENT_ADDR, Abi.dmx(SERVICE_NAME));
                    if (Abi.failed(hr)) {
                        Harness.log("SERVER", "FATAL: Dmx listen failed (%s)", Abi.name(hr));
                        return Harness.EXIT_SETUP;
                    }
                    Harness.log("SERVER", "Dmx service '%s' armed for '%s'", SERVICE_NAME, CLIENT_ADDR);

                    // ---- Hub B: CLIENT --------------------------------------
                    client.onPeerUp(p -> {
                        Harness.log("CLIENT", "peer up   : %s - Dmx connection ready", p);
                        int sent = client.sendText(SERVER_ADDR, TOPIC,
                                "Hello over Dmx - a real connection with no OS handle!");
                        Harness.log("CLIENT", "sendText  : %s", Abi.name(sent));
                    });
                    client.onPeerDown(p -> Harness.log("CLIENT", "peer down : %s", p));
                    client.onError   (w -> Harness.log("CLIENT", "error     : %s", w));

                    hr = client.connect(SERVER_ADDR, Abi.dmx(SERVICE_NAME));
                    if (Abi.failed(hr)) {
                        Harness.log("CLIENT", "FATAL: Dmx connect failed (%s)", Abi.name(hr));
                        return Harness.EXIT_SETUP;
                    }
                    Harness.log("CLIENT", "dialled the Dmx service");

                    Harness.log("MAIN", "waiting up to 10s for connect + delivery...");
                    boolean ok = done.await(10_000);

                    Harness.log("MAIN", "shutdown begin");
                    return Harness.verdict(ok,
                            "server received the client's message over Dmx",
                            "no message delivered (handshake did not complete)");
                }
            }
        });
    }
}

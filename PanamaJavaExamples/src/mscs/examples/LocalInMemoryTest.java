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
// LocalInMemoryTest.java  (Java -- Panama FFI over TargetFacade)
//
// TWO HUBS, ONE PROCESS, NO WIRE -- bidirectional delivery with no socket, no
// pipe and no OS handle of any kind.
//
// READ THIS BEFORE COMPARING TO THE ORIGINAL. This is the one harness in the set
// whose MECHANISM could not be carried across, only its RESULT.
//
//   The original used the kernel's pump-injection call:
//       PostP2Pmsg( pMsg, targetHub.GetHubID() )
//   which drops a message straight onto another hub's pump queue, bypassing the
//   connection layer entirely -- no P2PeerCon, no login handshake. It also came
//   with a sharp edge the original documented at length: the call is only legal
//   from a NON-hub thread, because doing it from inside one hub's pump to target
//   a different hub trips a cross-hub-context ASSERT.
//
//   The facade deliberately does not expose that. Its model is that hubs talk
//   over connections, so there is no GetHubID(), no raw pump queue, and no way
//   to violate the threading rule. The nearest thing it offers is the Dmx
//   transport: an in-address-space connection with no OS handle. So this version
//   reaches the same observable end state (two in-process hubs exchange messages
//   both ways, nothing leaves the address space) by a different route -- and pays
//   one login handshake for it.
//
//   If you specifically need pump injection, that is a reason to use Targetcore
//   directly; see DirectExamples\LocalInMemoryTest. Note that the flat C API
//   Targetcore already publishes for FFI consumers (Targetcore_c.h) does not
//   expose it either, so "use the kernel directly" currently means C++.
//
// THE ORDERING RULE THAT REPLACED THE THREADING ONE. There IS still a rule, and
// it is not the one the original had:
//
//     onPeerUp on the LISTENING side can fire BEFORE the dialling side has
//     finished logging in. Sending from it races the handshake, and the far end
//     answers an early message with
//         "Application message received before login / Connection dropped out"
//     -- it drops the whole connection.
//
// So the first message on a link must come from the side that DIALLED (its
// peer-up means login-ack, which is strictly later), and the listening side
// should answer on receipt rather than announce itself. That is the shape used
// below, and the same shape PipeMsgMapTest uses for its ping/pong.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : BOTH hubs received the message addressed to them.
//   3 = TIMEOUT : one or both deliveries did not complete in time.
//   1 = SETUP   : the network or a hub could not be created / armed.
package mscs.examples;

import mscs.harness.Harness;
import mscs.p2pf.Abi;
import mscs.p2pf.Hub;
import mscs.p2pf.Network;

public final class LocalInMemoryTest {

    private static final String SERVICE_NAME = "P2PlocalMeshJava";
    private static final String ADDR_HUB_A   = "LocalMesh.HubA";
    private static final String ADDR_HUB_B   = "LocalMesh.HubB";
    private static final String TOPIC        = "local";

    public static void main(String[] args) {
        Harness.run("LocalInMemoryTest", () -> {
            Harness.banner("=== LocalInMemoryTest (Java) - two hubs, one process, no wire ===");
            Harness.banner("Delivery mechanism: Dmx connection (in-address-space, no OS handle)");
            Harness.banner("");

            Harness.Gate recvA = new Harness.Gate();
            Harness.Gate recvB = new Harness.Gate();

            try (Network net = Network.open()) {
                Harness.log("MAIN", "facade : %s", net.versionString());

                try (Hub hubA = net.createHub(ADDR_HUB_A);
                     Hub hubB = net.createHub(ADDR_HUB_B)) {

                    // ---- Hub A -- armed first, since Dmx dials do not retry ----
                    // A is the LISTENING side, so it must not speak first (see
                    // the header). It answers on receipt instead, which is what
                    // makes the A -> B leg safe.
                    hubA.onTopic(TOPIC, m -> {
                        Harness.logMessage("HubA", "message", m.source, m.text());
                        recvA.open();

                        int h = hubA.sendText(ADDR_HUB_B, TOPIC,
                                              "Hello HubB - delivered in memory, no wire!");
                        Harness.log("HubA", "A -> B  : %s", Abi.name(h));
                    });
                    hubA.onPeerUp(p -> Harness.log("HubA", "peer up : %s", p));
                    hubA.onError (w -> Harness.log("HubA", "error : %s", w));

                    int hr = hubA.listen(ADDR_HUB_B, Abi.dmx(SERVICE_NAME));
                    if (Abi.failed(hr)) {
                        Harness.log("HubA", "FATAL: Dmx listen failed (%s)", Abi.name(hr));
                        return Harness.EXIT_SETUP;
                    }

                    // ---- Hub B ----------------------------------------------
                    hubB.onTopic(TOPIC, m -> {
                        Harness.logMessage("HubB", "message", m.source, m.text());
                        recvB.open();
                    });
                    hubB.onError(w -> Harness.log("HubB", "error : %s", w));

                    // B DIALLED, so its peer-up means login-ack: it is the side
                    // that may speak first. One Dmx connection carries traffic
                    // both ways, so B's opener plus A's answer prove delivery in
                    // both directions, as the original did.
                    hubB.onPeerUp(p -> {
                        Harness.log("HubB", "peer up : %s - in-process link ready", p);
                        int h = hubB.sendText(ADDR_HUB_A, TOPIC,
                                              "Hello HubA - same process, straight to your pump!");
                        Harness.log("HubB", "B -> A  : %s", Abi.name(h));
                    });

                    hr = hubB.connect(ADDR_HUB_A, Abi.dmx(SERVICE_NAME));
                    if (Abi.failed(hr)) {
                        Harness.log("HubB", "FATAL: Dmx connect failed (%s)", Abi.name(hr));
                        return Harness.EXIT_SETUP;
                    }
                    Harness.log("MAIN", "both hubs up, linked in memory");

                    // ---- Wait for BOTH deliveries ---------------------------
                    Harness.log("MAIN", "waiting up to 10s for both in-memory deliveries...");
                    boolean ok = Harness.awaitAll(10_000, recvA, recvB);

                    Harness.log("MAIN", "shutdown begin");
                    return Harness.verdict(ok,
                            "both hubs received their in-memory message",
                            "one or both deliveries did not complete");
                }
            }
        });
    }
}

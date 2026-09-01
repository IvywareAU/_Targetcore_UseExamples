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
// TwoConTest.java  (Java -- Panama FFI over TargetFacade)
//
// Can a SINGLE hub supervise TWO connections? And what happens if the second one
// names a peer the hub already has?
//
// The original answered this by reading P2PeerHub.cpp:432-447 and then proving
// it at runtime: a hub owns a LIST of connections, so two is fine IN PRINCIPLE,
// but PostP2PeerCon REJECTS a connection whose identification address (the
// REMOTE peer address) duplicates one already posted.
//
// That rule survives into the facade as a named error, which is the whole reason
// P2PF_E_CON_DUPLICATE exists in the public header. So this version asks the same
// two questions with no source archaeology required:
//
//   PART A (positive) : listen(PeerA) and connect(PeerB) on ONE hub -> both S_OK.
//                       Over loopback the hub then logs in to itself.
//   PART B (negative) : arm a THIRD connection whose peer address duplicates
//                       PeerA -> P2PF_E_CON_DUPLICATE, by name, in the return
//                       value. No TRUE/FALSE to interpret, no kernel source to
//                       cross-reference.
//
// Note both Listen and Connect take the peer address as their FIRST argument in
// the facade, which makes the rule the test is probing visible at the call site:
// on Listen it is who you EXPECT to dial in, on Connect it is who you are
// dialling. Distinct peers, distinct connections.
//
// Verdict by EXIT CODE:
//   0 = PASS  : both distinct-peer arms succeeded AND the duplicate was refused.
//   3 = FAIL  : an arm that should have succeeded failed, or the duplicate was
//               NOT refused -- i.e. behaviour differs from the contract.
//   1 = SETUP : the network or the hub could not be created.
package mscs.examples;

import mscs.harness.Harness;
import mscs.p2pf.Abi;
import mscs.p2pf.Hub;
import mscs.p2pf.Network;

public final class TwoConTest {

    private static final int PORT     = 7788;
    private static final int ALT_PORT = 7789;

    private static final String HUB_ADDR = "TwoConTest.Hub";
    private static final String SVC_PEER = "TwoConTest.PeerA";   // the listener's expected peer
    private static final String CLI_PEER = "TwoConTest.PeerB";   // the dialler's target peer

    public static void main(String[] args) {
        Harness.run("TwoConTest", () -> {
            Harness.banner("=== TwoConTest (Java) - one hub, two connections ===");
            Harness.banner("Hub  : " + HUB_ADDR);
            Harness.banner("PeerA: " + SVC_PEER + " (listen :" + PORT + ")");
            Harness.banner("PeerB: " + CLI_PEER + " (dial 127.0.0.1:" + PORT + ")");
            Harness.banner("");

            Harness.Gate login = new Harness.Gate();

            try (Network net = Network.open()) {
                Harness.log("MAIN", "facade : %s", net.versionString());

                try (Hub hub = net.createHub(HUB_ADDR)) {
                    hub.onPeerUp(p -> {
                        Harness.log("HUB", "peer up   : %s", p);
                        login.open();
                    });
                    hub.onPeerDown(p -> Harness.log("HUB", "peer down : %s", p));
                    hub.onError   (w -> Harness.log("HUB", "error     : %s", w));

                    // ---- PART A : two connections, distinct peer addresses ----
                    Harness.banner("--- PART A: two connections with DISTINCT peer addresses ---");

                    String svcEp = Abi.tcpListen(PORT);
                    int hrSvc = hub.listen(SVC_PEER, svcEp);
                    Harness.log("HUB", "listen ( '%s', '%s' )  -> %s",
                                SVC_PEER, svcEp, Abi.name(hrSvc));

                    String cliEp = Abi.tcpDial("127.0.0.1", PORT);
                    int hrCli = hub.connect(CLI_PEER, cliEp);
                    Harness.log("HUB", "connect( '%s', '%s' ) -> %s",
                                CLI_PEER, cliEp, Abi.name(hrCli));

                    boolean partA = !Abi.failed(hrSvc) && !Abi.failed(hrCli);
                    Harness.banner("    => PART A " + (partA ? "PASS" : "FAIL")
                                   + ": one hub is driving two connections");
                    Harness.banner("");

                    // Informational, exactly as in the original: over loopback
                    // the hub's own dial reaches its own listener, so it logs in
                    // to itself. The verdict does not depend on it.
                    boolean selfLogin = login.await(5_000);
                    Harness.log("MAIN", "self-login over loopback: %s",
                                selfLogin ? "observed" : "not observed (informational only)");

                    // ---- PART B : a third connection duplicating PeerA -------
                    Harness.banner("");
                    Harness.banner("--- PART B: a THIRD connection duplicating PeerA ---");

                    // A DIFFERENT endpoint, the SAME peer address: the duplicate
                    // rule is keyed on the peer, never on the transport or port.
                    String dupEp = Abi.tcpListen(ALT_PORT);
                    int hrDup = hub.listen(SVC_PEER, dupEp);
                    Harness.log("HUB", "listen ( '%s', '%s' )  -> %s",
                                SVC_PEER, dupEp, Abi.name(hrDup));

                    boolean partB = (hrDup == Abi.E_CON_DUPLICATE);
                    Harness.banner("    => PART B " + (partB ? "PASS" : "FAIL")
                                   + ": a duplicate peer address is "
                                   + (partB ? "refused by name" : "NOT refused as documented"));
                    Harness.banner("");

                    Harness.log("MAIN", "shutdown begin");
                    return Harness.verdict(partA && partB,
                            "two connections on one hub, duplicate peer refused",
                            "behaviour differs from the documented contract");
                }
            }
        });
    }
}

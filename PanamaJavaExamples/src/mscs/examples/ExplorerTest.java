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
// ExplorerTest.java  (Java -- Panama FFI over TargetFacade)
//
// "Ask a hub what it is." The twelfth harness, and the one the Light tree left
// out altogether -- so this file has no C++ facade twin to be read beside, only
// the original.
//
// ===========================================================================
// WHAT WAS LOST, AND WHY IT COULD NOT BE CARRIED ACROSS
// ===========================================================================
//
// The original (DirectExamples\ExplorerTest, 502 lines -- the largest harness
// in the tree) stands up a P2PeerExplorer on a hub with P2PeerExpump_ACTIVATE,
// then has an ANONYMOUS client log in, be assigned a slot address out of the
// "CEX.XC*" domain, and send P2PexpumpCtrl control messages. The Explorer
// answers with P2PexpumpHub messages whose content is a P3PmsgItem TREE hanging
// off the message's data node -- machine name, executable, hub registry -- which
// the client walks with P2PmsgRecurs and prints.
//
// None of that has a facade spelling, and the gap is not a small one:
//
//   1. THE FACADE HAS NO EXPLORER SURFACE AT ALL. Grep TargetFacade.h for
//      "expump" or "explorer" and there are no hits. There is no way to
//      activate one, and no way to adopt a hub that has one.
//
//   2. THE ANSWER TRAVELS WHERE THE FACADE CANNOT LOOK. A facade Message is a
//      payload (bytes) plus, since ABI 8, named fields. The Explorer's reply
//      carries a P3PmsgItem subtree on r_datn() instead -- the Msgcore object
//      model, one layer below the facade, which the facade deliberately does not
//      re-export.
//
//   3. THE CLIENT MUST BE ANONYMOUS AND GET ITS ADDRESS FROM THE SERVER. Every
//      facade hub is created with an address it keeps. There is no
//      createHub("") that later becomes "CEX.XC0".
//
// IP2PHub::GetNative(void**) does hand back the underlying P2PeerHub*, which is
// the documented escape hatch for exactly this -- but a raw C++ object pointer
// is worth nothing from Java: reaching P2PeerExpump_ACTIVATE through it would
// mean calling a mangled C++ free function and walking a class whose vtable this
// binding does not transcribe. That is the point at which "no new native code"
// stops being possible, and this tree's premise is that it does not stop.
//
// So this harness does what the Light tree's RouteLoopbackTest does with ITS
// untranslatable original: it asks the same QUESTION of the facade, using the
// facade's own answer to it -- the ABI 6 read side, GetConCount / GetCon /
// GetEndpoint / Describe.
//
// The comparison is worth making explicit, because the two answers are not the
// same shape:
//
//   P2PeerExplorer          | the facade read side
//   ------------------------|---------------------------------------------
//   a REMOTE peer asks      | the LOCAL process asks
//   over a real message     | over a method call, no traffic
//   answers a P3PmsgItem    | answers strings and a flag word
//   tree: machine, exe,     | peer, endpoint, listen/dial, up, and the
//   hub registry           | ancestor/descendant relation
//   needs an anonymous      | needs nothing
//   login and a slot        |
//
// What the facade gives up is remoteness -- nothing here crosses a wire, so this
// cannot answer "what is that OTHER machine running". What it gains is the
// topology relation, which the Explorer never reported: P2PF_REL_* names whether
// each peer is this hub's ancestor, descendant or neither, and a wrongly-shaped
// link is exactly the bug that connects, logs in and looks healthy.
//
// ===========================================================================
//
// The mesh below is deliberately mixed so the read side has something to say:
//
//   CEX            -- the hub under inspection
//    +-- CEX.Child -- a DESCENDANT, dialled in over Dmx and up
//   Other.Peer     -- UNRELATED, armed but never dialled (so: not up)
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : the hub reported both peers, with the right relation and the
//                 right liveness for each, and Describe() agreed with GetCon().
//   3 = FAIL    : the read side disagreed with the mesh that was built.
//   1 = SETUP   : the mesh could not be built.
package mscs.examples;

import java.util.ArrayList;
import java.util.List;

import mscs.harness.Harness;
import mscs.p2pf.Abi;
import mscs.p2pf.Hub;
import mscs.p2pf.Network;

public final class ExplorerTest {

    private static final String HUB_ADDR   = "CEX";
    private static final String CHILD_ADDR = "CEX.Child";
    private static final String OTHER_ADDR = "Other.Peer";

    private static final String SVC_CHILD = "P2PexpProbeJava";

    private static boolean check(String label, boolean ok) {
        Harness.banner("    => " + (ok ? "PASS" : "FAIL") + ": " + label);
        return ok;
    }

    public static void main(String[] args) {
        Harness.run("ExplorerTest", () -> {
            Harness.banner("=== ExplorerTest (Java) - asking a hub what it is ===");
            Harness.banner("Hub      : " + HUB_ADDR);
            Harness.banner("Child    : " + CHILD_ADDR + "  (descendant, dialled in over Dmx)");
            Harness.banner("Other    : " + OTHER_ADDR + "  (unrelated, armed but never dialled)");
            Harness.banner("Mechanism: the facade read side, NOT P2PeerExplorer -- see the header");
            Harness.banner("");

            Harness.Gate childUp = new Harness.Gate();

            try (Network net = Network.open()) {
                Harness.log("MAIN", "facade : %s", net.versionString());

                try (Hub cex   = net.createHub(HUB_ADDR);
                     Hub child = net.createHub(CHILD_ADDR)) {

                    cex.onPeerUp(p -> {
                        Harness.log("CEX", "peer up   : %s", p);
                        if (CHILD_ADDR.equals(p)) childUp.open();
                    });
                    cex.onPeerDown(p -> Harness.log("CEX", "peer down : %s", p));
                    cex.onError   (w -> Harness.log("CEX", "error     : %s", w));
                    child.onError (w -> Harness.log("CHILD", "error     : %s", w));

                    // Arm the listener the child will dial. Dmx dials do not
                    // retry, so this has to come first.
                    int hr = cex.listen(CHILD_ADDR, Abi.dmx(SVC_CHILD));
                    if (Abi.failed(hr)) {
                        Harness.log("MAIN", "FATAL: listen(child) failed (%s)", Abi.name(hr));
                        return Harness.EXIT_SETUP;
                    }

                    // A second peer that is armed and will never come up. This
                    // is the case the header of GetCon calls out: the endpoint
                    // reported is what was REQUESTED, and liveness is a separate
                    // bit. A harness that only ever looks at healthy meshes
                    // cannot tell those two apart.
                    hr = cex.listen(OTHER_ADDR, Abi.tcpListen(7834));
                    if (Abi.failed(hr)) {
                        Harness.log("MAIN", "FATAL: listen(other) failed (%s)", Abi.name(hr));
                        return Harness.EXIT_SETUP;
                    }

                    hr = child.connect(HUB_ADDR, Abi.dmx(SVC_CHILD));
                    if (Abi.failed(hr)) {
                        Harness.log("MAIN", "FATAL: child connect failed (%s)", Abi.name(hr));
                        return Harness.EXIT_SETUP;
                    }

                    if (!childUp.await(10_000)) {
                        Harness.log("MAIN", "TIMEOUT - the child never logged in");
                        return Harness.EXIT_TIMEOUT;
                    }

                    boolean allOk = true;

                    // ---- [1] The peer table, entry by entry -----------------
                    Harness.banner("");
                    Harness.banner("[1] GetConCount / GetCon -- the peers this hub knows");
                    int n = cex.conCount();
                    Harness.banner("    GetConCount -> " + n);

                    List<Hub.Con> cons = new ArrayList<>();
                    for (int i = 0; i < n; i++) {
                        Hub.Con c = cex.getCon(i);
                        if (c == null) continue;
                        cons.add(c);
                        Harness.banner("    [" + i + "] " + c);
                    }
                    allOk &= check("the hub reports at least the two peers it armed",
                                   cons.size() >= 2);

                    // ---- [2] The relation bits, which the Explorer never had ----
                    Harness.banner("");
                    Harness.banner("[2] P2PF_REL_* -- where each peer sits in the address tree");
                    Hub.Con kid   = cons.stream().filter(c -> CHILD_ADDR.equals(c.peer())).findFirst().orElse(null);
                    Hub.Con other = cons.stream().filter(c -> OTHER_ADDR.equals(c.peer())).findFirst().orElse(null);

                    boolean relOk = kid   != null && (kid.flags()   & Abi.REL_MASK) == Abi.REL_DESCENDANT
                                 && other != null && (other.flags() & Abi.REL_MASK) == Abi.REL_UNRELATED;
                    Harness.banner("    " + CHILD_ADDR + " -> "
                                   + (kid   == null ? "MISSING" : Abi.conFlags(kid.flags())));
                    Harness.banner("    " + OTHER_ADDR + " -> "
                                   + (other == null ? "MISSING" : Abi.conFlags(other.flags())));
                    allOk &= check("'CEX.Child' reads as a descendant and 'Other.Peer' as unrelated",
                                   relOk);

                    // ---- [3] Liveness is separate from arming ---------------
                    Harness.banner("");
                    Harness.banner("[3] P2PF_CON_UP -- armed is not the same as up");
                    boolean upOk = kid != null && (kid.flags() & Abi.CON_UP) != 0
                                && other != null && (other.flags() & Abi.CON_UP) == 0;
                    allOk &= check("the dialled child is up, the never-dialled peer is not", upOk);

                    // ---- [4] The endpoint round-trips ----------------------
                    Harness.banner("");
                    Harness.banner("[4] GetEndpoint -- canonically spelled, so it goes back into listen()");
                    String ep = cex.getEndpoint(CHILD_ADDR);
                    Harness.banner("    GetEndpoint('" + CHILD_ADDR + "') -> '" + ep + "'");
                    allOk &= check("the armed Dmx endpoint comes back naming its service",
                                   ep != null && ep.contains(SVC_CHILD));

                    // ---- [5] Describe(), the atomic snapshot ---------------
                    // The Explorer's whole reason for existing was to hand back
                    // ONE consistent picture rather than a series of reads that
                    // a login could slip between. This is the facade's version
                    // of that promise, and it is the form to put in a log.
                    Harness.banner("");
                    Harness.banner("[5] Describe() -- one atomic snapshot, the form for a log or bug report");
                    String desc = cex.describe();
                    for (String line : desc.split("\\R")) Harness.banner("    | " + line);
                    allOk &= check("Describe() names the hub and both peers",
                                   desc.contains("address=" + HUB_ADDR)
                                   && desc.contains(CHILD_ADDR) && desc.contains(OTHER_ADDR));

                    Harness.banner("");
                    Harness.log("MAIN", "shutdown begin");
                    return Harness.verdict(allOk,
                            "the hub described itself, its peers and their relations",
                            "the read side disagreed with the mesh that was built");
                }
            }
        });
    }
}

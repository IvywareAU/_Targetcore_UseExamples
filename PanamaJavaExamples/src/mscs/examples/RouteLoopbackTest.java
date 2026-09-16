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
// RouteLoopbackTest.java  (Java -- Panama FFI over TargetFacade)
//
// FOUR HUBS IN A TWO-LEVEL TREE, one process, no OS handles -- and the question
// is whether a message finds its way between two hubs that are NOT directly
// connected.
//
//              Root
//             /    \
//            A      B
//            |
//          Leaf
//
// READ THIS FIRST: THE ORIGINAL IS NOT AN MSCS HARNESS. Every other example in
// DirectExamples drives Targetcore. RouteLoopbackTest does not: it exercises
// treehub_runtime's PeerNetwork::route(), the clean-room in-process router the
// code generator emits generated projects against, with no P2PeerHub, no
// PostP2Pmsg and no pump thread anywhere. Its own header says so.
//
// So there is no facade translation of that code -- there is nothing of
// Targetcore in it to put a facade over. What this file does instead, following
// the Light tree, is ask the original's QUESTION of the real kernel: build the
// same four-node tree out of facade hubs, wire the same parent<->child edges
// over Dmx (the in-process transport, closest to route()'s DIRECT
// copy-on-deliver), and see how MSCS itself handles the scenarios.
//
// The addresses are hierarchical and dotted, which is what makes this a tree
// rather than four unrelated peers: "Root.A.Leaf" is a child of "Root.A" is a
// child of "Root". Routing and broadcast relay both key off that.
//
//   [1] Leaf -> B    : non-adjacent. Up to the root, then down.
//   [2] Root -> Leaf : straight down the tree, one intermediate hop.
//   [3] Leaf -> Z    : nobody by that address -- must NOT be delivered anywhere.
//   [4] Root broadcast : relayed down to every descendant. No counterpart in the
//                        original; it is the tree-shaped behaviour MSCS has and
//                        the generated router does not.
//
// THE ONE THING THIS FILE HAS TO DO THAT THE C++ VERSION DOES NOT. Four hubs
// means four pump threads calling into four Java sinks at once, so the delivery
// counters really are shared mutable state across threads -- the C++ version
// reaches for InterlockedIncrement for exactly this reason and this one reaches
// for AtomicInteger. Scenario [3] is the one that depends on it: it proves a
// negative by summing all four counters before and after, and a lost increment
// would turn a routing bug into a PASS.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : all four scenarios behaved as expected.
//   3 = FAIL    : a message was delivered somewhere it should not have been, or
//                 failed to arrive where it should have.
//   1 = SETUP   : the mesh could not be built.
package mscs.examples;

import java.util.concurrent.atomic.AtomicInteger;

import mscs.harness.Harness;
import mscs.p2pf.Abi;
import mscs.p2pf.Hub;
import mscs.p2pf.Network;

public final class RouteLoopbackTest {

    private static final String ROOT = "RouteMesh.Root";
    private static final String A    = "RouteMesh.Root.A";
    private static final String B    = "RouteMesh.Root.B";
    private static final String LEAF = "RouteMesh.Root.A.Leaf";
    private static final String Z    = "RouteMesh.Root.Z";      // nobody

    private static final String SVC_ROOT_A = "RM_RootA_Java";
    private static final String SVC_ROOT_B = "RM_RootB_Java";
    private static final String SVC_A_LEAF = "RM_ALeaf_Java";

    private static final String TOPIC = "route";

    /** One recorder per node: what arrived, and from where. */
    private static final class Node {
        final String        name;
        final AtomicInteger count = new AtomicInteger();
        final Harness.Gate  gate  = new Harness.Gate();
        Node(String name) { this.name = name; }
    }

    private static void attach(Hub hub, Node node) {
        hub.onTopic(TOPIC, m -> {
            node.count.incrementAndGet();
            Harness.banner(String.format("    [deliver] %s received \"%s\" (src=%s%s)",
                    node.name, m.text(), m.source, m.broadcast ? ", broadcast" : ""));
            node.gate.open();
        });
        hub.onError(w -> Harness.banner("    [error]   at " + node.name + ": " + w));
    }

    private static boolean check(String label, boolean ok) {
        Harness.banner("    => " + (ok ? "PASS" : "FAIL") + ": " + label);
        Harness.banner("");
        return ok;
    }

    public static void main(String[] args) {
        Harness.run("RouteLoopbackTest", () -> {
            Harness.banner("=== RouteLoopbackTest (Java) - four hubs, one process, tree routing ===");
            Harness.banner("        " + ROOT);
            Harness.banner("       /        \\");
            Harness.banner("    " + A + "   " + B);
            Harness.banner("      |");
            Harness.banner("   " + LEAF);
            Harness.banner("");

            Node nRoot = new Node(ROOT), nA = new Node(A), nB = new Node(B), nLeaf = new Node(LEAF);

            try (Network net = Network.open()) {
                Harness.log("MAIN", "facade : %s", net.versionString());

                // ---- Build the tree. Listeners first: dmx:// dials do not retry. ----
                //
                // Every edge here joins a parent to a child, which is what makes
                // the routing this harness measures possible at all: the
                // kernel's onward rules require an ancestor/descendant relation,
                // so a sibling edge would carry direct traffic and still refuse
                // to be a transit hop. The arming verbs classify each pair and
                // would say so -- every call below returns a plain S_OK.
                String epRootA = Abi.dmx(SVC_ROOT_A);
                String epRootB = Abi.dmx(SVC_ROOT_B);
                String epALeaf = Abi.dmx(SVC_A_LEAF);

                try (Hub root = net.createHub(ROOT);
                     Hub a    = net.createHub(A);
                     Hub b    = net.createHub(B);
                     Hub leaf = net.createHub(LEAF)) {

                    attach(root, nRoot);
                    int hr = root.listen(A, epRootA);
                    if (!Abi.failed(hr)) hr = root.listen(B, epRootB);
                    if (Abi.failed(hr)) {
                        Harness.log("MAIN", "FATAL: root Dmx listen failed (%s)", Abi.name(hr));
                        return Harness.EXIT_SETUP;
                    }

                    attach(a, nA);
                    hr = a.listen(LEAF, epALeaf);           // arm before Leaf dials
                    if (!Abi.failed(hr)) hr = a.connect(ROOT, epRootA);
                    if (Abi.failed(hr)) {
                        Harness.log("MAIN", "FATAL: A wiring failed (%s)", Abi.name(hr));
                        return Harness.EXIT_SETUP;
                    }

                    attach(b, nB);
                    hr = b.connect(ROOT, epRootB);
                    if (Abi.failed(hr)) {
                        Harness.log("MAIN", "FATAL: B wiring failed (%s)", Abi.name(hr));
                        return Harness.EXIT_SETUP;
                    }

                    attach(leaf, nLeaf);
                    hr = leaf.connect(A, epALeaf);
                    if (Abi.failed(hr)) {
                        Harness.log("MAIN", "FATAL: Leaf wiring failed (%s)", Abi.name(hr));
                        return Harness.EXIT_SETUP;
                    }

                    // Give the three links their login handshakes.
                    Harness.sleep(1_500);
                    Harness.banner("Links: Root<->A " + (root.isPeerUp(A)   ? "up" : "DOWN")
                                 + "   Root<->B "     + (root.isPeerUp(B)   ? "up" : "DOWN")
                                 + "   A<->Leaf "     + (a.isPeerUp(LEAF)   ? "up" : "DOWN"));
                    Harness.banner("");

                    boolean allOk = true;

                    // ---- [1] Non-adjacent, up then down: Leaf -> B ----------
                    Harness.banner("[1] Leaf -> " + B + "  (non-adjacent: up to the root, then down)");
                    hr = leaf.sendText(B, TOPIC, "hello from the leaf");
                    Harness.banner("    sendText returned " + Abi.name(hr));
                    boolean got1 = nB.gate.await(5_000);
                    allOk &= check("Leaf -> B delivered across two hops", got1 && nB.count.get() == 1);

                    // ---- [2] Straight down: Root -> Leaf --------------------
                    Harness.banner("[2] Root -> " + LEAF + "  (straight down the tree)");
                    hr = root.sendText(LEAF, TOPIC, "ping, down we go");
                    Harness.banner("    sendText returned " + Abi.name(hr));
                    boolean got2 = nLeaf.gate.await(5_000);
                    allOk &= check("Root -> Leaf delivered via A", got2 && nLeaf.count.get() == 1);

                    // ---- [3] No such address -------------------------------
                    Harness.banner("[3] Leaf -> " + Z + "  (unknown branch; must not be delivered)");
                    int before = nRoot.count.get() + nA.count.get()
                               + nB.count.get()   + nLeaf.count.get();
                    hr = leaf.sendText(Z, TOPIC, "lost, no such node");
                    Harness.banner("    sendText returned " + Abi.name(hr));
                    Harness.sleep(1_500);
                    int after = nRoot.count.get() + nA.count.get()
                              + nB.count.get()   + nLeaf.count.get();
                    allOk &= check("Leaf -> Z delivered to nobody", before == after);

                    // ---- [4] Broadcast relayed down the tree ----------------
                    Harness.banner("[4] Root broadcast  (relayed to every descendant -- no counterpart");
                    Harness.banner("    in the original, this is the tree behaviour MSCS adds)");
                    int aBefore = nA.count.get(), bBefore = nB.count.get(),
                        leafBefore = nLeaf.count.get();
                    hr = root.broadcastText(TOPIC, "all hands");
                    Harness.banner("    broadcast returned " + Abi.name(hr));
                    Harness.sleep(2_000);
                    Harness.banner("    reached: A " + (nA.count.get()    > aBefore    ? "yes" : "no")
                                 + "   B "           + (nB.count.get()    > bBefore    ? "yes" : "no")
                                 + "   Leaf "        + (nLeaf.count.get() > leafBefore ? "yes" : "no"));
                    allOk &= check("broadcast reached both direct children",
                                   nA.count.get() > aBefore && nB.count.get() > bBefore);

                    Harness.log("MAIN", "shutdown begin");
                    return Harness.verdict(allOk,
                            "the tree routed every scenario as expected",
                            "a route did not match expectations");
                }
            }
        });
    }
}

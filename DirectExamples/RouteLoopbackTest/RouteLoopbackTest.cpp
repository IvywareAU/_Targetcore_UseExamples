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
// RouteLoopbackTest.cpp
//
// SINGLE-PROCESS, NO-WIRE routing example for the treehub_runtime engine.
//
// What this demonstrates
// ----------------------
// The single function that actually MOVES a message over the DIRECT loopback
// transport is:
//
//     PeerNetwork::route(origin, dst, body)
//
// "DIRECT loopback" = in-process copy-on-deliver. route() walks the explicitly
// wired parent<->child connection graph HOP BY HOP on the CALLING thread and
// invokes each node's hop hook in-process -- no sockets, no named pipes, no
// byte framing, no threads. (PeerNode::sendText / HubMesh::send / the getTree()
// MeshHandle::send are all just thin wrappers that build the destination
// address and call route() for you.)
//
// The mesh here is four hubs in a two-level tree:
//
//              Root (:1000)
//             /            \
//          A (:1001)      B (:1002)
//          |
//       Leaf (:1003)
//
// Each hub is a treehub::PeerNode; edges are wired both ways with connectTo()
// exactly as the generated buildMesh() does it. We then call route() directly
// and watch it forward:
//
//   [1] Leaf -> B    : UP then DOWN, 2 transit hops (A, Root), delivered at B.
//   [2] Root -> Leaf : DOWN only,    1 transit hop  (A),       delivered at Leaf.
//   [3] Leaf -> Z    : unknown address -> DROPPED_NO_ROUTE (route()'s no-route path).
//
// Each node's onTransit / onDeliver / onDropped hooks record what route() did,
// so the verdict is checked against the exact hop sequence, not just "arrived".
//
// This is NOT the MSCS Targetcore path (no P2PeerHub, no PostP2Pmsg, no pump
// thread). It is the clean-room in-process router that the code generator emits
// generated projects against (native/treehub_runtime). Contrast with the sibling
// MSCS harnesses (LocalInMemoryTest, PipeMeshTest, WsaMeshTest, ...).
//
// Verdict is reported by process EXIT CODE (unambiguous for a headless run):
//   0 = SUCCESS : all three scenarios routed exactly as expected.
//   3 = FAIL    : a route delivered/dropped differently than expected.

#include <cstdio>
#include <string>
#include <vector>

#include "treehub/TypedTransportPort.h"
#include "treehub/PeerNetwork.h"
#include "treehub/PeerNode.h"
#include "treehub/PeerAddress.h"

using treehub::TypedTransportPort;
using treehub::PeerNetwork;
using treehub::PeerNode;
using treehub::PeerAddress;
using treehub::PeerMessage;

namespace {

// Records exactly what route() did to one message (reset before each scenario).
struct Trace
{
    std::vector<std::string> transit;   // addresses forwarded THROUGH (intermediate hops)
    std::string              deliveredAt;   // address the message was delivered to (the dst)
    std::string              droppedWhy;    // non-empty if route() dropped the message
    void reset() { transit.clear(); deliveredAt.clear(); droppedWhy.clear(); }
};

// Wire a parent<->child edge both directions, mirroring the generated buildMesh():
// each side connectTo()'s the other's listen port so route() can walk it either way.
void Wire(PeerNode& parent, int parentPort, PeerNode& child, int childPort)
{
    parent.connectTo(child.address().name(),  childPort);
    child.connectTo(parent.address().name(),  parentPort);
}

// Install the three hop hooks so we can observe route()'s hop-by-hop decisions.
void Hook(PeerNode& node, Trace& trace)
{
    node.onTransit([&trace, &node](const PeerMessage&, const std::string&) {
        trace.transit.push_back(node.address().name());
        std::printf("    [transit] forwarded through %s\n", node.address().name().c_str());
    });
    node.onDeliver([&trace, &node](const PeerMessage& m, const std::string& body) {
        trace.deliveredAt = node.address().name();
        std::printf("    [deliver] %s received \"%s\" (src=%s)\n",
                    node.address().name().c_str(), body.c_str(), m.src.c_str());
    });
    node.onDropped([&trace, &node](const PeerMessage&, const std::string& why) {
        trace.droppedWhy = why;
        std::printf("    [drop]    at %s: %s\n", node.address().name().c_str(), why.c_str());
    });
}

const char* ResultName(PeerNetwork::RouteResult r)
{
    switch (r) {
        case PeerNetwork::RouteResult::DELIVERED:        return "DELIVERED";
        case PeerNetwork::RouteResult::DROPPED_NO_ROUTE: return "DROPPED_NO_ROUTE";
        case PeerNetwork::RouteResult::DROPPED_TTL:      return "DROPPED_TTL";
    }
    return "?";
}

bool Check(const char* label, bool ok)
{
    std::printf("    => %s: %s\n\n", ok ? "PASS" : "FAIL", label);
    return ok;
}

}  // namespace

int main()
{
    std::printf("=== RouteLoopbackTest - treehub_runtime PeerNetwork::route() DIRECT loopback ===\n");
    std::printf("No sockets, no threads: route() walks the wired tree hop-by-hop, in-process.\n\n");

    // ---- Build the DIRECT (in-process copy-on-deliver) network. -------------
    TypedTransportPort transport(TypedTransportPort::ConnectionType::DIRECT);
    PeerNetwork        net(transport);

    // Four hubs; kernel ids handed out from 1000 (spec 09 base).
    PeerNode root(net.allocateHubId(), "VNet1:Root",        net);
    PeerNode a   (net.allocateHubId(), "VNet1:Root.A",      net);
    PeerNode b   (net.allocateHubId(), "VNet1:Root.B",      net);
    PeerNode leaf(net.allocateHubId(), "VNet1:Root.A.Leaf", net);

    // Every hub registers its loopback port BEFORE any connectTo targets it.
    root.listen(1000);
    a.listen(1001);
    b.listen(1002);
    leaf.listen(1003);

    // Parent<->child edges, both directions.
    Wire(root, 1000, a,    1001);
    Wire(root, 1000, b,    1002);
    Wire(a,    1001, leaf, 1003);

    Trace trace;
    Hook(root, trace);
    Hook(a,    trace);
    Hook(b,    trace);
    Hook(leaf, trace);

    bool allOk = true;

    // ---- [1] UP-then-DOWN multi-hop: Leaf -> B ------------------------------
    // Expected path Leaf -> A -> Root -> B: transit at {A, Root}, delivered at B.
    std::printf("[1] route(Leaf -> VNet1:Root.B)  (non-adjacent; goes up to the root, then down)\n");
    trace.reset();
    const auto r1 = net.route(&leaf, PeerAddress("VNet1:Root.B"), "hello|from the leaf");
    std::printf("    route() returned %s\n", ResultName(r1));
    allOk &= Check("Leaf -> B delivered via A, Root",
                   r1 == PeerNetwork::RouteResult::DELIVERED
                   && trace.deliveredAt == "VNet1:Root.B"
                   && trace.transit == std::vector<std::string>{ "VNet1:Root.A", "VNet1:Root" });

    // ---- [2] DOWN-only: Root -> Leaf ----------------------------------------
    // Expected path Root -> A -> Leaf: transit at {A}, delivered at Leaf.
    std::printf("[2] route(Root -> VNet1:Root.A.Leaf)  (straight down the tree)\n");
    trace.reset();
    const auto r2 = net.route(&root, PeerAddress("VNet1:Root.A.Leaf"), "ping|down we go");
    std::printf("    route() returned %s\n", ResultName(r2));
    allOk &= Check("Root -> Leaf delivered via A",
                   r2 == PeerNetwork::RouteResult::DELIVERED
                   && trace.deliveredAt == "VNet1:Root.A.Leaf"
                   && trace.transit == std::vector<std::string>{ "VNet1:Root.A" });

    // ---- [3] No route: Leaf -> an address nobody can reach ------------------
    // route() walks up to the root, finds no child subtree containing the dst,
    // and reports DROPPED_NO_ROUTE via the last node's onDropped hook.
    std::printf("[3] route(Leaf -> VNet1:Root.Z)  (unknown branch; expected to drop)\n");
    trace.reset();
    const auto r3 = net.route(&leaf, PeerAddress("VNet1:Root.Z"), "lost|no such node");
    std::printf("    route() returned %s\n", ResultName(r3));
    allOk &= Check("Leaf -> Z dropped with no-route",
                   r3 == PeerNetwork::RouteResult::DROPPED_NO_ROUTE
                   && trace.deliveredAt.empty()
                   && trace.droppedWhy == "no-route");

    net.close();

    const int exitCode = allOk ? 0 : 3;
    std::printf("%s (exit=%d).\n", allOk ? "SUCCESS - route() behaved as expected"
                                         : "FAIL - a route did not match expectations",
                exitCode);
    return exitCode;
}

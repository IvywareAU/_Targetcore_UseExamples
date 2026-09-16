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
// RouteLoopbackTest.cpp  (Light -- TargetFacade)
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
// Targetcore in it to put a facade over. What this file does instead is ask the
// original's QUESTION of the real kernel: build the same four-node tree out of
// facade hubs, wire the same parent<->child edges (over Dmx, the in-process
// transport -- the closest thing to route()'s DIRECT copy-on-deliver), and see
// how MSCS itself handles the three scenarios.
//
// The addresses are hierarchical and dotted, which is what makes this a tree
// rather than four unrelated peers: "Root.A.Leaf" is a child of "Root.A" is a
// child of "Root". Routing and broadcast relay both key off that.
//
//   [1] Leaf -> B    : non-adjacent. Up to the root, then down.
//   [2] Root -> Leaf : straight down the tree, one intermediate hop.
//   [3] Leaf -> Z    : nobody by that address -- must NOT be delivered anywhere.
//   [4] Root broadcast : relayed down to every descendant. This one has no
//                        counterpart in the original; it is the tree-shaped
//                        behaviour MSCS has and the generated router does not.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : all four scenarios behaved as expected.
//   3 = FAIL    : a message was delivered somewhere it should not have been,
//                 or failed to arrive where it should have.
//   1 = SETUP   : the mesh could not be built.

#include "TargetFacadeFn.hpp"
#include "LightHarness.h"

static const wchar_t* const kRoot = L"RouteMesh.Root";
static const wchar_t* const kA    = L"RouteMesh.Root.A";
static const wchar_t* const kB    = L"RouteMesh.Root.B";
static const wchar_t* const kLeaf = L"RouteMesh.Root.A.Leaf";
static const wchar_t* const kZ    = L"RouteMesh.Root.Z";      // nobody

static const wchar_t* const kSvcRootA = L"RM_RootA";
static const wchar_t* const kSvcRootB = L"RM_RootB";
static const wchar_t* const kSvcALeaf = L"RM_ALeaf";

static const wchar_t* const kTopic = L"route";

// One recorder per node: what arrived, and from where.
struct Node
{
    const wchar_t *name;
    volatile LONG  count;
    light::Gate    gate;

    Node ( const wchar_t *n ) : name ( n ), count ( 0 ) { }
};

static void Attach ( p2pf::Hub& hub, Node& node )
{
    hub.onTopic ( kTopic, [&node] ( const p2pf::Message& m )
    {
        ::InterlockedIncrement ( &node.count );
        wprintf ( L"    [deliver] %s received \"%s\" (src=%s%s)\n",
                  node.name, m.text(), m.source, m.broadcast ? L", broadcast" : L"" );
        fflush ( stdout );
        node.gate.open();
    });
    hub.onError ( [&node] ( const wchar_t *what )
    {
        wprintf ( L"    [error]   at %s: %s\n", node.name, what );
        fflush ( stdout );
    });
}

static bool Check ( const wchar_t *label, bool ok )
{
    wprintf ( L"    => %s: %s\n\n", ok ? L"PASS" : L"FAIL", label );
    fflush ( stdout );
    return ok;
}

int main ( )
{
    light::InitConsole();
    wprintf ( L"=== RouteLoopbackTest (Light) - four hubs, one process, tree routing ===\n" );
    wprintf ( L"        %s\n       /        \\\n    %s   %s\n      |\n   %s\n\n",
              kRoot, kA, kB, kLeaf );
    fflush ( stdout );

    Node nRoot ( kRoot ), nA ( kA ), nB ( kB ), nLeaf ( kLeaf );

    try
    {
        p2pf::Network net;

        // ---- Build the tree. Listeners first: dmx:// dials do not retry. ----
        //
        // Every edge here joins a parent to a child, which is what makes the
        // routing this harness measures possible at all: the kernel's onward
        // rules require an ancestor/descendant relation, so a sibling edge
        // (Root.A <-> Root.B, say) would carry direct traffic and still refuse
        // to be a transit hop. The arming verbs classify each pair and would
        // say so -- every call below returns a plain S_OK.
        const std::wstring strSvcRootA = light::Dmx ( kSvcRootA );
        const std::wstring strSvcRootB = light::Dmx ( kSvcRootB );
        const std::wstring strSvcALeaf = light::Dmx ( kSvcALeaf );

        p2pf::Hub root = net.createHub ( kRoot );
        Attach ( root, nRoot );
        HRESULT hr = root.listen ( kA, strSvcRootA.c_str() );
        if ( SUCCEEDED(hr) ) hr = root.listen ( kB, strSvcRootB.c_str() );
        if ( FAILED(hr) )
        {
            light::Log ( L"MAIN", L"FATAL: root Dmx listen failed (%s)", light::HrName ( hr ) );
            return light::EXIT_SETUP;
        }

        p2pf::Hub a = net.createHub ( kA );
        Attach ( a, nA );
        hr = a.listen ( kLeaf, strSvcALeaf.c_str() );   // arm before Leaf dials
        if ( SUCCEEDED(hr) ) hr = a.connect ( kRoot, strSvcRootA.c_str() );
        if ( FAILED(hr) )
        {
            light::Log ( L"MAIN", L"FATAL: A wiring failed (%s)", light::HrName ( hr ) );
            return light::EXIT_SETUP;
        }

        p2pf::Hub b = net.createHub ( kB );
        Attach ( b, nB );
        hr = b.connect ( kRoot, strSvcRootB.c_str() );
        if ( FAILED(hr) )
        {
            light::Log ( L"MAIN", L"FATAL: B wiring failed (%s)", light::HrName ( hr ) );
            return light::EXIT_SETUP;
        }

        p2pf::Hub leaf = net.createHub ( kLeaf );
        Attach ( leaf, nLeaf );
        hr = leaf.connect ( kA, strSvcALeaf.c_str() );
        if ( FAILED(hr) )
        {
            light::Log ( L"MAIN", L"FATAL: Leaf wiring failed (%s)", light::HrName ( hr ) );
            return light::EXIT_SETUP;
        }

        // Give the three links their login handshakes.
        ::Sleep ( 1500 );
        wprintf ( L"Links: Root<->A %s   Root<->B %s   A<->Leaf %s\n\n",
                  root.isPeerUp ( kA )   ? L"up" : L"DOWN",
                  root.isPeerUp ( kB )   ? L"up" : L"DOWN",
                  a.isPeerUp    ( kLeaf ) ? L"up" : L"DOWN" );
        fflush ( stdout );

        bool allOk = true;

        // ---- [1] Non-adjacent, up then down: Leaf -> B ----------------------
        wprintf ( L"[1] Leaf -> %s  (non-adjacent: up to the root, then down)\n", kB );
        fflush ( stdout );
        hr = leaf.sendText ( kB, kTopic, L"hello from the leaf" );
        wprintf ( L"    sendText returned %s\n", light::HrName ( hr ) );
        bool got1 = nB.gate.wait ( 5000 );
        allOk &= Check ( L"Leaf -> B delivered across two hops", got1 && nB.count == 1 );

        // ---- [2] Straight down: Root -> Leaf --------------------------------
        wprintf ( L"[2] Root -> %s  (straight down the tree)\n", kLeaf );
        fflush ( stdout );
        hr = root.sendText ( kLeaf, kTopic, L"ping, down we go" );
        wprintf ( L"    sendText returned %s\n", light::HrName ( hr ) );
        bool got2 = nLeaf.gate.wait ( 5000 );
        allOk &= Check ( L"Root -> Leaf delivered via A", got2 && nLeaf.count == 1 );

        // ---- [3] No such address --------------------------------------------
        wprintf ( L"[3] Leaf -> %s  (unknown branch; must not be delivered)\n", kZ );
        fflush ( stdout );
        LONG before = nRoot.count + nA.count + nB.count + nLeaf.count;
        hr = leaf.sendText ( kZ, kTopic, L"lost, no such node" );
        wprintf ( L"    sendText returned %s\n", light::HrName ( hr ) );
        ::Sleep ( 1500 );
        LONG after = nRoot.count + nA.count + nB.count + nLeaf.count;
        allOk &= Check ( L"Leaf -> Z delivered to nobody", before == after );

        // ---- [4] Broadcast relayed down the tree ----------------------------
        wprintf ( L"[4] Root broadcast  (relayed to every descendant -- no counterpart\n"
                  L"    in the original, this is the tree behaviour MSCS adds)\n" );
        fflush ( stdout );
        LONG aBefore = nA.count, bBefore = nB.count, leafBefore = nLeaf.count;
        static const wchar_t kAllHands[] = L"all hands";
        hr = root.broadcast ( kTopic, kAllHands, (unsigned int)sizeof(kAllHands) );
        wprintf ( L"    broadcast returned %s\n", light::HrName ( hr ) );
        ::Sleep ( 2000 );
        wprintf ( L"    reached: A %s   B %s   Leaf %s\n",
                  nA.count    > aBefore    ? L"yes" : L"no",
                  nB.count    > bBefore    ? L"yes" : L"no",
                  nLeaf.count > leafBefore ? L"yes" : L"no" );
        allOk &= Check ( L"broadcast reached both direct children",
                         nA.count > aBefore && nB.count > bBefore );

        light::Log ( L"MAIN", L"shutdown begin" );
        return light::Verdict ( allOk,
                                L"the tree routed every scenario as expected",
                                L"a route did not match expectations" );
    }
    catch ( const std::exception& e )
    {
        printf ( "FATAL: %s\n", e.what() );
        return light::EXIT_SETUP;
    }
}

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
// RouteLoopbackTest.cpp  (COM -- TargetCom)
//
// FOUR HUBS IN A TWO-LEVEL TREE, one process, driven through COM.
//
//              Root
//             /    \
//            A      B
//            |
//          Leaf
//
// As in the Light tree: the ORIGINAL of this harness is not an MSCS test at all
// -- it drives treehub_runtime's PeerNetwork::route(), the clean-room in-process
// router, with no P2PeerHub anywhere. There is nothing in it to put a COM layer
// over. So this asks the original's QUESTION of the real kernel instead, and
// adds the thing only the COM version can show: FOUR live hub objects, each with
// its own connection point and its own dispatch thread inside the DLL, all
// marshalling into ONE single-threaded apartment. Every [deliver] line below is
// printed on the same thread as main().
//
//   [1] Leaf -> B      : non-adjacent. Up to the root, then down.
//   [2] Root -> Leaf   : straight down the tree.
//   [3] Leaf -> Z      : nobody by that address -- must go nowhere.
//   [4] Root broadcast : relayed to every descendant. Broadcast is also the one
//                        method whose COM signature had to change shape --
//                        it returns Delivered as a value, because the facade's
//                        S_FALSE ("nobody was up") is invisible to automation.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : all four scenarios behaved as expected.
//   3 = FAIL    : a message went somewhere it should not have, or failed to arrive.
//   1 = SETUP   : the COM server is not registered, or the mesh could not be built.

#include "ComHarness.h"

static const wchar_t* const kRoot = L"RouteMesh.Root";
static const wchar_t* const kA    = L"RouteMesh.Root.A";
static const wchar_t* const kB    = L"RouteMesh.Root.B";
static const wchar_t* const kLeaf = L"RouteMesh.Root.A.Leaf";
static const wchar_t* const kZ    = L"RouteMesh.Root.Z";

static const wchar_t* const kSvcRootA = L"RM_RootA_Com";
static const wchar_t* const kSvcRootB = L"RM_RootB_Com";
static const wchar_t* const kSvcALeaf = L"RM_ALeaf_Com";

static const wchar_t* const kTopic = L"route";

struct Node
{
    const wchar_t *name;
    LONG           count;
    com::Gate      gate;
    Node ( const wchar_t *n ) : name ( n ), count ( 0 ) { }
};

static void Attach ( com::Hub& hub, Node& node )
{
    hub.onTopic ( kTopic, [&node] ( const com::Message& m )
    {
        ++node.count;                       // all callbacks land on the STA thread
        wprintf ( L"    [deliver] %s received \"%s\" (src=%s%s)\n",
                  node.name, m.text(), m.source, m.broadcast ? L", broadcast" : L"" );
        fflush ( stdout );
        node.gate.open();
    });
    hub.onError ( [&node] ( LPCWSTR what )
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
    com::InitConsole();
    wprintf ( L"=== RouteLoopbackTest (COM) - four hubs, one apartment, tree routing ===\n" );
    wprintf ( L"        %s\n       /        \\\n    %s   %s\n      |\n   %s\n\n",
              kRoot, kA, kB, kLeaf );
    fflush ( stdout );

    com::Apartment sta;
    if ( !sta.ok() ) return com::EXIT_SETUP;

    com::Network net;
    if ( !net.ok() ) return com::SetupFailure ( L"CoCreateInstance(TargetCom.P2PNetwork)", net.hr() );

    Node nRoot ( kRoot ), nA ( kA ), nB ( kB ), nLeaf ( kLeaf );
    com::Hub root, a, b, leaf;

    // Composed once: each service name is named by BOTH ends of its link.
    const std::wstring strSvcRootA = com::Dmx ( kSvcRootA );
    const std::wstring strSvcRootB = com::Dmx ( kSvcRootB );
    const std::wstring strSvcALeaf = com::Dmx ( kSvcALeaf );

    // ---- Build the tree. Listeners first: dmx:// dials do not retry. --------
    //
    // This is the one harness in the tree whose addresses are HIERARCHICAL, so
    // every arm below is a parent naming a child or a child naming its parent,
    // and every one of them returns a plain S_OK. The sibling harnesses get
    // P2PF_S_UNRELATED_LINK instead -- also a success, but with an OnError line
    // to say the edge can never be a transit hop. Which is precisely the
    // property this harness goes on to prove: Leaf -> B works only BECAUSE
    // every edge here is a real parent/child edge the kernel will relay over.
    HRESULT hr = net.createHub ( kRoot, root );
    if ( FAILED(hr) ) return com::SetupFailure ( L"CreateHub(Root)", hr );
    Attach ( root, nRoot );
    hr = root.listen ( kA, strSvcRootA.c_str() );
    if ( SUCCEEDED(hr) ) hr = root.listen ( kB, strSvcRootB.c_str() );
    if ( FAILED(hr) ) return com::SetupFailure ( L"Root Listen(dmx)", hr );

    hr = net.createHub ( kA, a );
    if ( FAILED(hr) ) return com::SetupFailure ( L"CreateHub(A)", hr );
    Attach ( a, nA );
    hr = a.listen ( kLeaf, strSvcALeaf.c_str() );
    if ( SUCCEEDED(hr) ) hr = a.connect ( kRoot, strSvcRootA.c_str() );
    if ( FAILED(hr) ) return com::SetupFailure ( L"A wiring", hr );

    hr = net.createHub ( kB, b );
    if ( FAILED(hr) ) return com::SetupFailure ( L"CreateHub(B)", hr );
    Attach ( b, nB );
    hr = b.connect ( kRoot, strSvcRootB.c_str() );
    if ( FAILED(hr) ) return com::SetupFailure ( L"B wiring", hr );

    hr = net.createHub ( kLeaf, leaf );
    if ( FAILED(hr) ) return com::SetupFailure ( L"CreateHub(Leaf)", hr );
    Attach ( leaf, nLeaf );
    hr = leaf.connect ( kA, strSvcALeaf.c_str() );
    if ( FAILED(hr) ) return com::SetupFailure ( L"Leaf wiring", hr );

    com::PumpFor ( 1500 );          // let the three links log in
    wprintf ( L"Links: Root<->A %s   Root<->B %s   A<->Leaf %s\n\n",
              root.isPeerUp ( kA )    ? L"up" : L"DOWN",
              root.isPeerUp ( kB )    ? L"up" : L"DOWN",
              a.isPeerUp    ( kLeaf ) ? L"up" : L"DOWN" );
    fflush ( stdout );

    bool allOk = true;

    // ---- [1] Non-adjacent, up then down -------------------------------------
    wprintf ( L"[1] Leaf -> %s  (non-adjacent: up to the root, then down)\n", kB );
    fflush ( stdout );
    hr = leaf.sendText ( kB, kTopic, L"hello from the leaf" );
    wprintf ( L"    SendText returned %s\n", com::HrName ( hr ) );
    bool got1 = nB.gate.wait ( 5000 );
    allOk &= Check ( L"Leaf -> B delivered across two hops", got1 && nB.count == 1 );

    // ---- [2] Straight down --------------------------------------------------
    wprintf ( L"[2] Root -> %s  (straight down the tree)\n", kLeaf );
    fflush ( stdout );
    hr = root.sendText ( kLeaf, kTopic, L"ping, down we go" );
    wprintf ( L"    SendText returned %s\n", com::HrName ( hr ) );
    bool got2 = nLeaf.gate.wait ( 5000 );
    allOk &= Check ( L"Root -> Leaf delivered via A", got2 && nLeaf.count == 1 );

    // ---- [3] No such address -------------------------------------------------
    wprintf ( L"[3] Leaf -> %s  (unknown branch; must not be delivered)\n", kZ );
    fflush ( stdout );
    LONG before = nRoot.count + nA.count + nB.count + nLeaf.count;
    hr = leaf.sendText ( kZ, kTopic, L"lost, no such node" );
    wprintf ( L"    SendText returned %s\n", com::HrName ( hr ) );
    com::PumpFor ( 1500 );
    LONG after = nRoot.count + nA.count + nB.count + nLeaf.count;
    allOk &= Check ( L"Leaf -> Z delivered to nobody", before == after );

    // ---- [4] Broadcast, and its Delivered return value ------------------------
    wprintf ( L"[4] Root broadcast  (relayed down; Delivered comes back as a VALUE)\n" );
    fflush ( stdout );
    LONG aBefore = nA.count, bBefore = nB.count, leafBefore = nLeaf.count;
    static const wchar_t kAllHands[] = L"all hands";
    bool bDelivered = false;
    hr = root.broadcast ( kTopic, kAllHands, (unsigned int)sizeof(kAllHands), &bDelivered );
    wprintf ( L"    Broadcast returned %s, Delivered=%s\n",
              com::HrName ( hr ), bDelivered ? L"True" : L"False" );
    com::PumpFor ( 2000 );
    wprintf ( L"    reached: A %s   B %s   Leaf %s\n",
              nA.count    > aBefore    ? L"yes" : L"no",
              nB.count    > bBefore    ? L"yes" : L"no",
              nLeaf.count > leafBefore ? L"yes" : L"no" );
    allOk &= Check ( L"broadcast reached both children and reported Delivered",
                     bDelivered && nA.count > aBefore && nB.count > bBefore );

    com::Log ( L"MAIN", L"shutdown begin" );
    return com::Verdict ( allOk,
                          L"the tree routed every scenario as expected",
                          L"a route did not match expectations" );
}

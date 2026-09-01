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
// TwoConTest.cpp  (Light -- TargetFacade)
//
// Can a SINGLE hub supervise TWO connections? And what happens if the second
// one names a peer the hub already has?
//
// The original answered this by reading P2PeerHub.cpp:432-447 and then proving
// it at runtime: a hub owns a LIST of connections, so two is fine IN PRINCIPLE,
// but PostP2PeerCon REJECTS a connection whose identification address (the
// REMOTE peer address) duplicates one already posted.
//
// That rule survives into the facade as a named error, which is the whole
// reason P2PF_E_CON_DUPLICATE exists in the public header. So the Light version
// asks the same two questions with no source archaeology required:
//
//   PART A (positive) : listen(PeerA) and connect(PeerB) on ONE hub -> both S_OK.
//                       Over loopback the hub then logs in to itself.
//   PART B (negative) : arm a THIRD connection whose peer address duplicates
//                       PeerA -> P2PF_E_CON_DUPLICATE, by name, in the return
//                       value. No TRUE/FALSE to interpret, no kernel source to
//                       cross-reference.
//
// Note both Listen and Connect take the peer address as their FIRST argument in
// the facade, which makes the rule the test is probing visible at the call
// site: on Listen it is who you EXPECT to dial in, on Connect it is who you are
// dialling. Distinct peers, distinct connections.
//
// Verdict by EXIT CODE:
//   0 = PASS    : both distinct-peer arms succeeded AND the duplicate was refused.
//   3 = FAIL    : an arm that should have succeeded failed, or the duplicate
//                 was NOT refused -- i.e. behaviour differs from the contract.
//   1 = SETUP   : the network or the hub could not be created.

#include "TargetFacadeFn.hpp"
#include "LightHarness.h"

static const unsigned short kPort    = 7788;
static const unsigned short kAltPort = 7789;

static const wchar_t* const kHubAddr = L"TwoConTest.Hub";
static const wchar_t* const kSvcPeer = L"TwoConTest.PeerA";   // the listener's expected peer
static const wchar_t* const kCliPeer = L"TwoConTest.PeerB";   // the dialler's target peer

int main ( )
{
    light::InitConsole();
    wprintf ( L"=== TwoConTest (Light) - one hub, two connections ===\n" );
    wprintf ( L"Hub  : %s\nPeerA: %s (listen :%d)\nPeerB: %s (dial 127.0.0.1:%d)\n\n",
              kHubAddr, kSvcPeer, (int)kPort, kCliPeer, (int)kPort );
    fflush ( stdout );

    light::Gate gLogin;

    try
    {
        p2pf::Network net;

        p2pf::Hub hub = net.createHub ( kHubAddr );
        hub.onPeerUp ( [&] ( const wchar_t *peer )
        {
            light::Log ( L"HUB", L"peer up   : %s", peer );
            gLogin.open();
        });
        hub.onPeerDown ( [] ( const wchar_t *peer ) { light::Log ( L"HUB", L"peer down : %s", peer ); } );
        hub.onError    ( [] ( const wchar_t *what ) { light::Log ( L"HUB", L"error     : %s", what ); } );

        // ---- PART A : two connections, distinct peer addresses --------------
        wprintf ( L"--- PART A: two connections with DISTINCT peer addresses ---\n" );
        fflush ( stdout );

        std::wstring strSvcEp = light::TcpListen ( kPort );
        HRESULT hrSvc = hub.listen ( kSvcPeer, strSvcEp.c_str() );
        light::Log ( L"HUB", L"listen ( '%s', '%s' )  -> %s",
                     kSvcPeer, strSvcEp.c_str(), light::HrName ( hrSvc ) );

        std::wstring strCliEp = light::TcpDial ( L"127.0.0.1", kPort );
        HRESULT hrCli = hub.connect ( kCliPeer, strCliEp.c_str() );
        light::Log ( L"HUB", L"connect( '%s', '%s' ) -> %s",
                     kCliPeer, strCliEp.c_str(), light::HrName ( hrCli ) );

        bool bPartA = SUCCEEDED(hrSvc) && SUCCEEDED(hrCli);
        wprintf ( L"    => PART A %s: one hub is driving two connections\n\n",
                  bPartA ? L"PASS" : L"FAIL" );
        fflush ( stdout );

        // Informational, exactly as in the original: over loopback the hub's
        // own dial reaches its own listener, so it logs in to itself. The
        // verdict does not depend on it.
        bool bSelfLogin = gLogin.wait ( 5000 );
        light::Log ( L"MAIN", L"self-login over loopback: %s",
                     bSelfLogin ? L"observed" : L"not observed (informational only)" );

        // ---- PART B : a third connection duplicating PeerA -------------------
        wprintf ( L"\n--- PART B: a THIRD connection duplicating PeerA ---\n" );
        fflush ( stdout );

        // A DIFFERENT endpoint, the SAME peer address: the duplicate rule is
        // keyed on the peer, never on the transport or the port.
        std::wstring strDupEp = light::TcpListen ( kAltPort );
        HRESULT hrDup = hub.listen ( kSvcPeer, strDupEp.c_str() );
        light::Log ( L"HUB", L"listen ( '%s', '%s' )  -> %s",
                     kSvcPeer, strDupEp.c_str(), light::HrName ( hrDup ) );

        bool bPartB = ( hrDup == p2pf::P2PF_E_CON_DUPLICATE );
        wprintf ( L"    => PART B %s: a duplicate peer address is %s\n\n",
                  bPartB ? L"PASS" : L"FAIL",
                  bPartB ? L"refused by name" : L"NOT refused as documented" );
        fflush ( stdout );

        light::Log ( L"MAIN", L"shutdown begin" );
        bool ok = bPartA && bPartB;
        return light::Verdict ( ok,
                                L"two connections on one hub, duplicate peer refused",
                                L"behaviour differs from the documented contract" );
    }
    catch ( const std::exception& e )
    {
        printf ( "FATAL: %s\n", e.what() );
        return light::EXIT_SETUP;
    }
}

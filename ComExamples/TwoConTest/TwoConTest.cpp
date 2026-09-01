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
// TwoConTest.cpp  (COM -- TargetCom)
//
// Can a SINGLE hub supervise TWO connections, and what happens when the second
// names a peer the hub already has?
//
// The original answered by reading P2PeerHub.cpp:432-447 and proving it at
// runtime through a TRUE/FALSE from PostP2PeerCon. The facade turned that into
// a named HRESULT. COM carries the same HRESULT out to every automation client
// unchanged -- which matters more than it sounds: a VBScript or C# caller sees
// a COMException whose HResult is 0x80040204, and can branch on it. There is no
// version of this test that needs the kernel source.
//
//   PART A (positive) : Listen(PeerA) and Connect(PeerB) on ONE hub -> both
//                       succeed. Note SUCCEEDED(), not == S_OK: these three
//                       addresses are siblings, so each arm returns the
//                       SUCCESS code P2PF_S_UNRELATED_LINK (0x0004020C).
//   PART B (negative) : a THIRD connection duplicating PeerA, on a DIFFERENT
//                       endpoint -> P2PF_E_CON_DUPLICATE (0x80040204). The
//                       different endpoint is the point: the rule keys on the
//                       peer ADDRESS, not on the port or the transport.
//
// The harness also exercises the COM-specific half of the same question: the
// connection point's own Advise/Unadvise bookkeeping, where a stale cookie is
// refused with CONNECT_E_NOCONNECTION.
//
// Verdict by EXIT CODE:
//   0 = PASS  : both distinct-peer arms succeeded, the duplicate was refused,
//               and the connection point refused a stale cookie.
//   3 = FAIL  : behaviour differs from the documented contract.
//   1 = SETUP : the COM server is not registered.

#include "ComHarness.h"

static const unsigned short kPort    = 7788;
static const unsigned short kAltPort = 7789;

static const wchar_t* const kHubAddr = L"TwoConTest.Hub";
static const wchar_t* const kSvcPeer = L"TwoConTest.PeerA";
static const wchar_t* const kCliPeer = L"TwoConTest.PeerB";

int main ( )
{
    com::InitConsole();
    wprintf ( L"=== TwoConTest (COM) - one hub, two connections ===\n" );
    wprintf ( L"Hub  : %s\nPeerA: %s (Listen :%d)\nPeerB: %s (Connect 127.0.0.1:%d)\n\n",
              kHubAddr, kSvcPeer, (int)kPort, kCliPeer, (int)kPort );
    fflush ( stdout );

    com::Apartment sta;
    if ( !sta.ok() ) return com::EXIT_SETUP;

    com::Network net;
    if ( !net.ok() ) return com::SetupFailure ( L"CoCreateInstance(TargetCom.P2PNetwork)", net.hr() );

    com::Gate gLogin;
    com::Hub  hub;

    HRESULT hr = net.createHub ( kHubAddr, hub );
    if ( FAILED(hr) ) return com::SetupFailure ( L"CreateHub", hr );

    hub.onPeerUp   ( [&] ( LPCWSTR peer ) { com::Log ( L"HUB", L"peer up   : %s", peer ); gLogin.open(); } );
    hub.onPeerDown ( []  ( LPCWSTR peer ) { com::Log ( L"HUB", L"peer down : %s", peer ); } );
    hub.onError    ( []  ( LPCWSTR what ) { com::Log ( L"HUB", L"error     : %s", what ); } );

    wprintf ( L"Address property reads back as '%s'\n\n", hub.address().c_str() );
    fflush ( stdout );

    // ---- PART A -------------------------------------------------------------
    wprintf ( L"--- PART A: two connections with DISTINCT peer addresses ---\n" );
    fflush ( stdout );

    HRESULT hrSvc = hub.listen ( kSvcPeer, com::TcpListen ( kPort ).c_str() );
    com::Log ( L"HUB", L"Listen ( '%s', 'tcp://:%d' )  -> %s",
               kSvcPeer, (int)kPort, com::HrName ( hrSvc ) );

    HRESULT hrCli = hub.connect ( kCliPeer, com::TcpDial ( L"127.0.0.1", kPort ).c_str() );
    com::Log ( L"HUB", L"Connect( '%s', 'tcp://127.0.0.1:%d' ) -> %s",
               kCliPeer, (int)kPort, com::HrName ( hrCli ) );

    bool bPartA = SUCCEEDED(hrSvc) && SUCCEEDED(hrCli);
    wprintf ( L"    => PART A %s: one hub is driving two connections\n\n", bPartA ? L"PASS" : L"FAIL" );
    fflush ( stdout );

    // Informational: over loopback the hub's own dial reaches its own listener.
    bool bSelfLogin = gLogin.wait ( 5000 );
    com::Log ( L"MAIN", L"self-login over loopback: %s",
               bSelfLogin ? L"observed" : L"not observed (informational only)" );

    // ---- PART B -------------------------------------------------------------
    wprintf ( L"\n--- PART B: a THIRD connection duplicating PeerA ---\n" );
    fflush ( stdout );

    // A DIFFERENT port, so nothing about the endpoint collides -- only the peer
    // address repeats. That is what is being refused.
    HRESULT hrDup = hub.listen ( kSvcPeer, com::TcpListen ( kAltPort ).c_str() );
    com::Log ( L"HUB", L"Listen ( '%s', 'tcp://:%d' )  -> %s (0x%08lX)",
               kSvcPeer, (int)kAltPort, com::HrName ( hrDup ), (unsigned long)hrDup );

    bool bPartB = ( hrDup == com::E_CON_DUPLICATE );
    wprintf ( L"    => PART B %s: a duplicate peer address is %s\n\n",
              bPartB ? L"PASS" : L"FAIL",
              bPartB ? L"refused by name (on a free port), out through COM unchanged"
                     : L"NOT refused as documented" );
    fflush ( stdout );

    // ---- PART C : the connection point's own bookkeeping ---------------------
    wprintf ( L"--- PART C: the connection point refuses a stale cookie ---\n" );
    fflush ( stdout );

    IConnectionPointContainer *pCPC = NULL;
    IConnectionPoint          *pCP  = NULL;
    bool bPartC = false;

    if ( SUCCEEDED ( hub.raw()->QueryInterface ( IID_IConnectionPointContainer, (void**)&pCPC ) ) )
    {
        if ( SUCCEEDED ( pCPC->FindConnectionPoint ( DIID__IP2PHubEvents, &pCP ) ) )
        {
            HRESULT hrStale = pCP->Unadvise ( 0xDEADBEEF );
            com::Log ( L"HUB", L"Unadvise(bogus cookie) -> %s",
                       hrStale == CONNECT_E_NOCONNECTION ? L"CONNECT_E_NOCONNECTION" : L"(other)" );
            bPartC = ( hrStale == CONNECT_E_NOCONNECTION );
            pCP->Release();
        }
        IConnectionPoint *pBogus = NULL;
        HRESULT hrNoIid = pCPC->FindConnectionPoint ( IID_IUnknown, &pBogus );
        com::Log ( L"HUB", L"FindConnectionPoint(IID_IUnknown) -> %s",
                   hrNoIid == CONNECT_E_NOCONNECTION ? L"CONNECT_E_NOCONNECTION" : L"(other)" );
        bPartC = bPartC && ( hrNoIid == CONNECT_E_NOCONNECTION );
        pCPC->Release();
    }
    wprintf ( L"    => PART C %s\n\n", bPartC ? L"PASS" : L"FAIL" );
    fflush ( stdout );

    com::Log ( L"MAIN", L"shutdown begin" );
    bool ok = bPartA && bPartB && bPartC;
    return com::Verdict ( ok,
                          L"two connections on one hub, duplicate peer refused, cookies checked",
                          L"behaviour differs from the documented contract" );
}

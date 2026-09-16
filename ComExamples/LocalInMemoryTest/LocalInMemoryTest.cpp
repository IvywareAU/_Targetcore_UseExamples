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
// LocalInMemoryTest.cpp  (COM -- TargetCom)
//
// TWO HUBS, ONE PROCESS, NO WIRE -- bidirectional delivery with no socket, no
// pipe and no OS handle, driven through COM.
//
// The same caveat as the Light version, one layer further out: the original
// used the kernel's pump-injection call PostP2Pmsg(msg, targetHub.GetHubID()),
// which bypasses the connection layer entirely. Neither the facade nor the COM
// layer exposes that -- the model is that hubs talk over connections -- so this
// reaches the same end state over the Dmx transport and pays one login
// handshake for it. If you need pump injection, use Targetcore directly.
//
// The ordering rule found while writing the Light tree applies here too, and it
// is worth restating because a COM client is even further from the evidence:
//
//     OnPeerUp on the LISTENING side can fire BEFORE the dialling side has
//     finished logging in. Sending from it races the handshake and the far end
//     kills the connection with "Application message received before login".
//
// So HubB (which dialled) speaks first and HubA answers on receipt.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : BOTH hubs received the message addressed to them.
//   3 = TIMEOUT : one or both deliveries did not complete in time.
//   1 = SETUP   : the COM server is not registered, or a hub could not be armed.

#include "ComHarness.h"

static const wchar_t* const kServiceName = L"P2PlocalMeshCom";
static const wchar_t* const kAddrHubA    = L"LocalMesh.HubA";
static const wchar_t* const kAddrHubB    = L"LocalMesh.HubB";
static const wchar_t* const kTopic       = L"local";

int main ( )
{
    com::InitConsole();
    wprintf ( L"=== LocalInMemoryTest (COM) - two hubs, one process, no wire ===\n" );
    wprintf ( L"Delivery mechanism: Dmx connection (in-address-space, no OS handle)\n\n" );
    fflush ( stdout );

    com::Apartment sta;
    if ( !sta.ok() ) return com::EXIT_SETUP;

    com::Network net;
    if ( !net.ok() ) return com::SetupFailure ( L"CoCreateInstance(TargetCom.P2PNetwork)", net.hr() );

    com::Gate gRecvA, gRecvB;
    com::Hub  hubA, hubB;

    // ---- Hub A: the LISTENING side. Answers, never opens. --------------------
    HRESULT hr = net.createHub ( kAddrHubA, hubA );
    if ( FAILED(hr) ) return com::SetupFailure ( L"CreateHub(HubA)", hr );

    hubA.onTopic ( kTopic, [&] ( const com::Message& m )
    {
        com::LogMessage ( L"HubA", L"message", m.source, m.text() );
        gRecvA.open();

        HRESULT h = hubA.sendText ( kAddrHubB, kTopic,
                                    L"Hello HubB - delivered in memory, no wire!" );
        com::Log ( L"HubA", L"A -> B  : %s", com::HrName ( h ) );
    });
    hubA.onPeerUp ( [] ( LPCWSTR peer ) { com::Log ( L"HubA", L"peer up : %s", peer ); } );
    hubA.onError  ( [] ( LPCWSTR what ) { com::Log ( L"HubA", L"error : %s", what ); } );

    hr = hubA.listen ( kAddrHubB, com::Dmx ( kServiceName ).c_str() );
    if ( FAILED(hr) ) return com::SetupFailure ( L"Listen(dmx)", hr );

    // ---- Hub B: the DIALLING side. Its peer-up means login-ack. --------------
    hr = net.createHub ( kAddrHubB, hubB );
    if ( FAILED(hr) ) return com::SetupFailure ( L"CreateHub(HubB)", hr );

    hubB.onTopic ( kTopic, [&] ( const com::Message& m )
    {
        com::LogMessage ( L"HubB", L"message", m.source, m.text() );
        gRecvB.open();
    });
    hubB.onPeerUp ( [&] ( LPCWSTR peer )
    {
        com::Log ( L"HubB", L"peer up : %s - in-process link ready", peer );
        HRESULT h = hubB.sendText ( kAddrHubA, kTopic,
                                    L"Hello HubA - same process, straight to your pump!" );
        com::Log ( L"HubB", L"B -> A  : %s", com::HrName ( h ) );
    });
    hubB.onError ( [] ( LPCWSTR what ) { com::Log ( L"HubB", L"error : %s", what ); } );

    hr = hubB.connect ( kAddrHubA, com::Dmx ( kServiceName ).c_str() );
    if ( FAILED(hr) ) return com::SetupFailure ( L"Connect(dmx)", hr );
    com::Log ( L"MAIN", L"both hubs up, linked in memory" );

    com::Log ( L"MAIN", L"waiting up to 10s for both in-memory deliveries (pumping)..." );
    com::Gate *both[2] = { &gRecvA, &gRecvB };
    bool ok = com::WaitAll ( both, 2, 10000 );

    com::Log ( L"MAIN", L"shutdown begin" );
    return com::Verdict ( ok,
                          L"both hubs received their in-memory message",
                          L"one or both deliveries did not complete" );
}

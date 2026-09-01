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
// LocalInMemoryTest.cpp  (Light -- TargetFacade)
//
// TWO HUBS, ONE PROCESS, NO WIRE -- bidirectional delivery with no socket, no
// pipe and no OS handle of any kind.
//
// READ THIS BEFORE COMPARING TO THE ORIGINAL. This is the one harness in the
// set whose MECHANISM could not be carried across, only its RESULT.
//
//   The original used the kernel's pump-injection call:
//       PostP2Pmsg( pMsg, targetHub.GetHubID() )
//   which drops a message straight onto another hub's pump queue, bypassing
//   the connection layer entirely -- no P2PeerCon, no login handshake. It also
//   came with a sharp edge the original documented at length: the call is only
//   legal from a NON-hub thread, because doing it from inside one hub's pump
//   to target a different hub trips a cross-hub-context ASSERT.
//
//   The facade deliberately does not expose that. Its model is that hubs talk
//   over connections, so there is no GetHubID(), no raw pump queue, and no way
//   to violate the threading rule. The nearest thing it offers is the Dmx
//   transport: an in-address-space connection with no OS handle. So this Light
//   version reaches the same observable end state (two in-process hubs
//   exchange messages both ways, nothing leaves the address space) by a
//   different route -- and pays one login handshake for it.
//
//   If you specifically need pump injection, that is a reason to use
//   TargetCore directly; see DirectExamples\LocalInMemoryTest.
//
// Note also what disappears: because every send below happens on a hub's own
// callback thread or on main, and the facade routes rather than injects, the
// original's "only call this from a non-hub thread" hazard cannot be expressed
// here at all.
//
// WHAT REPLACES IT (learned by writing this file, so you do not have to). There
// IS still an ordering rule, and it is not the one the original had:
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
// below, and the same shape PipeMsgMapTest (Light) uses for its ping/pong.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : BOTH hubs received the message addressed to them.
//   3 = TIMEOUT : one or both deliveries did not complete in time.
//   1 = SETUP   : the network or a hub could not be created / armed.

#include "TargetFacadeFn.hpp"
#include "LightHarness.h"

static const wchar_t* const kServiceName = L"P2PlocalMeshLight";
static const wchar_t* const kAddrHubA    = L"LocalMesh.HubA";
static const wchar_t* const kAddrHubB    = L"LocalMesh.HubB";
static const wchar_t* const kTopic       = L"local";

int main ( )
{
    light::InitConsole();
    wprintf ( L"=== LocalInMemoryTest (Light) - two hubs, one process, no wire ===\n" );
    wprintf ( L"Delivery mechanism: Dmx connection (in-address-space, no OS handle)\n\n" );
    fflush ( stdout );

    light::Gate gRecvA, gRecvB;

    try
    {
        p2pf::Network net;

        // ---- Hub A -- armed first, since Dmx dials do not retry -------------
        p2pf::Hub hubA = net.createHub ( kAddrHubA );

        // A is the LISTENING side, so it must not speak first (see the header).
        // It answers on receipt instead, which is what makes the A -> B leg
        // safe.
        hubA.onTopic ( kTopic, [&] ( const p2pf::Message& m )
        {
            light::LogMessage ( L"HubA", L"message", m.source, m.text() );
            gRecvA.open();

            HRESULT h = hubA.sendText ( kAddrHubB, kTopic,
                                        L"Hello HubB - delivered in memory, no wire!" );
            light::Log ( L"HubA", L"A -> B  : %s", light::HrName ( h ) );
        });
        hubA.onPeerUp ( [] ( const wchar_t *peer ) { light::Log ( L"HubA", L"peer up : %s", peer ); } );
        hubA.onError  ( [] ( const wchar_t *what ) { light::Log ( L"HubA", L"error : %s", what ); } );

        HRESULT hr = hubA.listen ( kAddrHubB, light::Dmx ( kServiceName ).c_str() );
        if ( FAILED(hr) )
        {
            light::Log ( L"HubA", L"FATAL: Dmx listen failed (%s)", light::HrName ( hr ) );
            return light::EXIT_SETUP;
        }

        // ---- Hub B ----------------------------------------------------------
        p2pf::Hub hubB = net.createHub ( kAddrHubB );
        hubB.onTopic ( kTopic, [&] ( const p2pf::Message& m )
        {
            light::LogMessage ( L"HubB", L"message", m.source, m.text() );
            gRecvB.open();
        });
        hubB.onError ( [] ( const wchar_t *what ) { light::Log ( L"HubB", L"error : %s", what ); } );

        // B DIALLED, so its peer-up means login-ack: it is the side that may
        // speak first. One Dmx connection carries traffic both ways, so B's
        // opener plus A's answer prove delivery in both directions, as the
        // original did.
        hubB.onPeerUp ( [&] ( const wchar_t *peer )
        {
            light::Log ( L"HubB", L"peer up : %s - in-process link ready", peer );
            HRESULT h = hubB.sendText ( kAddrHubA, kTopic,
                                        L"Hello HubA - same process, straight to your pump!" );
            light::Log ( L"HubB", L"B -> A  : %s", light::HrName ( h ) );
        });

        hr = hubB.connect ( kAddrHubA, light::Dmx ( kServiceName ).c_str() );
        if ( FAILED(hr) )
        {
            light::Log ( L"HubB", L"FATAL: Dmx connect failed (%s)", light::HrName ( hr ) );
            return light::EXIT_SETUP;
        }
        light::Log ( L"MAIN", L"both hubs up, linked in memory" );

        // ---- Wait for BOTH deliveries ---------------------------------------
        light::Log ( L"MAIN", L"waiting up to 10s for both in-memory deliveries..." );
        light::Gate *both[2] = { &gRecvA, &gRecvB };
        bool ok = light::WaitAll ( both, 2, 10000 );

        light::Log ( L"MAIN", L"shutdown begin" );
        return light::Verdict ( ok,
                                L"both hubs received their in-memory message",
                                L"one or both deliveries did not complete" );
    }
    catch ( const std::exception& e )
    {
        printf ( "FATAL: %s\n", e.what() );
        return light::EXIT_SETUP;
    }
}

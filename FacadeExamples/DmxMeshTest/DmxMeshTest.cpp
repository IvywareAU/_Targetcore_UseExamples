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
// DmxMeshTest.cpp  (Light -- TargetFacade)
//
// SINGLE-PROCESS, TWO-HUB in-process CONNECTION probe (Dmx transport).
//
// Third instance of the same shape, third endpoint scheme: "dmx://service".
// Dmx is the in-address-space rendezvous transport -- a real connection with a
// real login handshake, but no OS handle at all; endpoints are matched by a
// shared SERVICE NAME rather than a port or a pipe path.
//
// ORDERING MATTERS HERE, and this is the one place the facade does not paper
// over it. Facade tcp:// and pipe:// dials retry, so arming order is
// irrelevant for those. A dmx:// dial does NOT retry: a missing in-process
// service is a configuration fault, not a timing one, so the listener must be
// armed first. That is exactly what the code below does, and it is documented
// on IP2PHub -- note that the retry policy follows the TRANSPORT, not the verb,
// which is why it survived the eight verbs collapsing onto two.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : the server hub received the client's message over Dmx.
//   3 = TIMEOUT : handshake or delivery did not complete in time.
//   1 = SETUP   : the network or a hub could not be created / armed.

#include "TargetFacadeFn.hpp"
#include "LightHarness.h"

static const wchar_t* const kServiceName = L"P2PdmxProbeLight";
static const wchar_t* const kServerAddr  = L"DmxMesh.Server";
static const wchar_t* const kClientAddr  = L"DmxMesh.Client";
static const wchar_t* const kTopic       = L"mesh";

int main ( )
{
    light::InitConsole();
    wprintf ( L"=== DmxMeshTest (Light) - two hubs, one process, Dmx transport ===\n" );
    wprintf ( L"Service : %s\n\n", kServiceName );
    fflush ( stdout );

    light::Gate gDone;

    try
    {
        p2pf::Network net;

        // ---- Hub A: SERVER -- armed FIRST (a dmx:// dial does not retry) ----
        p2pf::Hub server = net.createHub ( kServerAddr );
        server.onTopic ( kTopic, [&] ( const p2pf::Message& m )
        {
            light::LogMessage ( L"SERVER", L"message", m.source, m.text() );
            gDone.open();
        });
        server.onPeerUp   ( [] ( const wchar_t *peer ) { light::Log ( L"SERVER", L"peer up   : %s", peer ); } );
        server.onPeerDown ( [] ( const wchar_t *peer ) { light::Log ( L"SERVER", L"peer down : %s", peer ); } );
        server.onError    ( [] ( const wchar_t *what ) { light::Log ( L"SERVER", L"error     : %s", what ); } );

        HRESULT hr = server.listen ( kClientAddr, light::Dmx ( kServiceName ).c_str() );
        if ( FAILED(hr) )
        {
            light::Log ( L"SERVER", L"FATAL: Dmx listen failed (%s)", light::HrName ( hr ) );
            return light::EXIT_SETUP;
        }
        light::Log ( L"SERVER", L"Dmx service '%s' armed for '%s'", kServiceName, kClientAddr );

        // ---- Hub B: CLIENT --------------------------------------------------
        p2pf::Hub client = net.createHub ( kClientAddr );
        client.onPeerUp ( [&] ( const wchar_t *peer )
        {
            light::Log ( L"CLIENT", L"peer up   : %s - Dmx connection ready", peer );
            HRESULT hrSend = client.sendText ( kServerAddr, kTopic,
                                               L"Hello over Dmx - a real connection with no OS handle!" );
            light::Log ( L"CLIENT", L"sendText  : %s", light::HrName ( hrSend ) );
        });
        client.onPeerDown ( [] ( const wchar_t *peer ) { light::Log ( L"CLIENT", L"peer down : %s", peer ); } );
        client.onError    ( [] ( const wchar_t *what ) { light::Log ( L"CLIENT", L"error     : %s", what ); } );

        hr = client.connect ( kServerAddr, light::Dmx ( kServiceName ).c_str() );
        if ( FAILED(hr) )
        {
            light::Log ( L"CLIENT", L"FATAL: Dmx connect failed (%s)", light::HrName ( hr ) );
            return light::EXIT_SETUP;
        }
        light::Log ( L"CLIENT", L"dialled the Dmx service" );

        light::Log ( L"MAIN", L"waiting up to 10s for connect + delivery..." );
        bool ok = gDone.wait ( 10000 );

        light::Log ( L"MAIN", L"shutdown begin" );
        return light::Verdict ( ok,
                                L"server received the client's message over Dmx",
                                L"no message delivered (handshake did not complete)" );
    }
    catch ( const std::exception& e )
    {
        printf ( "FATAL: %s\n", e.what() );
        return light::EXIT_SETUP;
    }
}

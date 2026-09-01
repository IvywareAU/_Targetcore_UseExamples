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
// WsaMeshTest.cpp  (Light -- TargetFacade)
//
// SINGLE-PROCESS, TWO-HUB LOOPBACK-TCP connectivity probe.
//
// Same question as DirectExamples\WsaMeshTest: can two hubs in ONE process
// connect over loopback TCP and complete the login handshake? Same verdict by
// exit code. What changed is everything below the question:
//
//   original (286 lines)                    light (this file)
//   ---------------------------------------------------------------------
//   CWinApp theApp + MFC stdafx             (gone)
//   StartupP2Pmsg + WSAStartup              p2pf::Network net;
//   P2PeerHub subclass + SpawnHub()         net.createHub(addr)
//   7 On_Con* trace overrides               hub.onPeerUp(...)
//   On_P2PeerBCast override                 hub.onTopic(L"mesh", ...)
//   ServiceFactory + PostP2PeerCon          hub.listen(peer, L"tcp://:port")
//   ClientFactory + PostP2PeerCon           hub.connect(peer, L"tcp://ip:port")
//   new P2PeerMsg32(...) + PostP2PeerMsg    hub.sendText(dest, topic, text)
//   _CrtSetReportHook assert trap           (unreachable -- no MFC here)
//   CloseHub/Wait/CloseHandle/Cleanup       (destructors, in order)
//
// One behavioural improvement falls out of the facade: the original had to
// Sleep(750) between arming the listener and dialling, because a raw
// ClientFactory connection dials exactly ONCE. Facade dials retry (~2/s), so
// the ordering below is not load-bearing -- see the facade README.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : the server hub received the client's message over loopback TCP.
//   3 = TIMEOUT : handshake or delivery did not complete in time.
//   1 = SETUP   : the network or a hub could not be created / armed.

#include "TargetFacadeFn.hpp"
#include "LightHarness.h"

static const unsigned short kTestPort   = 7801;
static const wchar_t* const kServerAddr = L"WsaMesh.Server";
static const wchar_t* const kClientAddr = L"WsaMesh.Client";
static const wchar_t* const kTopic      = L"mesh";

int main ( )
{
    light::InitConsole();
    wprintf ( L"=== WsaMeshTest (Light) - two hubs, one process, loopback TCP ===\n" );
    wprintf ( L"Port : %d (127.0.0.1)\n\n", (int)kTestPort );
    fflush ( stdout );

    light::Gate gDone;          // opened when the SERVER receives the message

    try
    {
        // ONE object replaces StartupP2Pmsg(16) + WSAStartup(2.2) and their
        // matching teardown calls on every exit path.
        p2pf::Network net;

        // ---- Hub A: SERVER --------------------------------------------------
        // createHub() constructs the hub AND spawns its pump thread; there is
        // no separate SpawnHub()/CloseHub() to get wrong.
        p2pf::Hub server = net.createHub ( kServerAddr );

        // This is the P2PeerMsg_MAP replacement. No macros, no subclass.
        server.onTopic ( kTopic, [&] ( const p2pf::Message& m )
        {
            light::LogMessage ( L"SERVER", L"message", m.source, m.text() );
            gDone.open();
        });
        server.onPeerUp   ( [] ( const wchar_t *peer ) { light::Log ( L"SERVER", L"peer up   : %s", peer ); } );
        server.onPeerDown ( [] ( const wchar_t *peer ) { light::Log ( L"SERVER", L"peer down : %s", peer ); } );
        server.onError    ( [] ( const wchar_t *what ) { light::Log ( L"SERVER", L"error     : %s", what ); } );

        HRESULT hr = server.listen ( kClientAddr, light::TcpListen ( kTestPort ).c_str() );
        if ( FAILED(hr) )
        {
            light::Log ( L"SERVER", L"FATAL: listen failed (%s)", light::HrName ( hr ) );
            return light::EXIT_SETUP;
        }
        light::Log ( L"SERVER", L"listening for '%s' on port %d", kClientAddr, (int)kTestPort );

        // ---- Hub B: CLIENT --------------------------------------------------
        p2pf::Hub client = net.createHub ( kClientAddr );

        // The handshake milestone the original watched for through
        // On_ConLoginAck is just this callback.
        client.onPeerUp ( [&] ( const wchar_t *peer )
        {
            light::Log ( L"CLIENT", L"peer up   : %s - loopback TCP connection ready", peer );
            HRESULT hrSend = client.sendText ( kServerAddr, kTopic,
                                               L"Hello over loopback TCP, in one process!" );
            light::Log ( L"CLIENT", L"sendText  : %s", light::HrName ( hrSend ) );
        });
        client.onPeerDown ( [] ( const wchar_t *peer ) { light::Log ( L"CLIENT", L"peer down : %s", peer ); } );
        client.onError    ( [] ( const wchar_t *what ) { light::Log ( L"CLIENT", L"error     : %s", what ); } );

        hr = client.connect ( kServerAddr, light::TcpDial ( L"127.0.0.1", kTestPort ).c_str() );
        if ( FAILED(hr) )
        {
            light::Log ( L"CLIENT", L"FATAL: connect failed (%s)", light::HrName ( hr ) );
            return light::EXIT_SETUP;
        }
        light::Log ( L"CLIENT", L"dialling 127.0.0.1:%d (retries until answered)", (int)kTestPort );

        // ---- Wait for the round trip ----------------------------------------
        light::Log ( L"MAIN", L"waiting up to 10s for connect + delivery..." );
        bool ok = gDone.wait ( 10000 );

        light::Log ( L"MAIN", L"shutdown begin" );
        return light::Verdict ( ok,
                                L"server received the client's message over loopback TCP",
                                L"no message delivered (handshake did not complete)" );
        // ~Hub closes each pump; ~Network shuts the kernel down. In order.
    }
    catch ( const std::exception& e )
    {
        printf ( "FATAL: %s\n", e.what() );
        return light::EXIT_SETUP;
    }
}

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
// DmxMeshTest.cpp  (COM -- TargetCom)
//
// SINGLE-PROCESS, TWO-HUB in-process CONNECTION probe (Dmx transport), over COM.
//
// A dmx:// dial does NOT retry -- a missing in-process service is a
// configuration fault, not a timing one -- so the listener is armed first.
//
// That rule used to be documented on IP2PHubCom's ListenDmx/ConnectDmx
// helpstrings, where an object browser would show it. With one Listen/Connect
// pair covering four transports there is no per-transport helpstring left to
// hang it on, so it lives here and in the facade README instead. That is the
// one thing collapsing the verbs cost: the type library can no longer explain
// a transport, only the endpoint grammar.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : the server hub received the client's message over Dmx.
//   3 = TIMEOUT : handshake or delivery did not complete in time.
//   1 = SETUP   : the COM server is not registered, or a hub could not be armed.

#include "ComHarness.h"

static const wchar_t* const kServiceName = L"P2PdmxProbeCom";
static const wchar_t* const kServerAddr  = L"DmxMesh.Server";
static const wchar_t* const kClientAddr  = L"DmxMesh.Client";
static const wchar_t* const kTopic       = L"mesh";

int main ( )
{
    com::InitConsole();
    wprintf ( L"=== DmxMeshTest (COM) - two hubs, one process, Dmx transport ===\n" );
    wprintf ( L"Service : %s   apartment: STA\n\n", kServiceName );
    fflush ( stdout );

    com::Apartment sta;
    if ( !sta.ok() ) return com::EXIT_SETUP;

    com::Network net;
    if ( !net.ok() ) return com::SetupFailure ( L"CoCreateInstance(TargetCom.P2PNetwork)", net.hr() );

    com::Gate gDone;
    com::Hub  server, client;

    // ---- SERVER: armed FIRST ------------------------------------------------
    HRESULT hr = net.createHub ( kServerAddr, server );
    if ( FAILED(hr) ) return com::SetupFailure ( L"CreateHub(server)", hr );

    server.onTopic ( kTopic, [&] ( const com::Message& m )
    {
        com::LogMessage ( L"SERVER", L"message", m.source, m.text() );
        gDone.open();
    });
    server.onPeerUp   ( [] ( LPCWSTR peer ) { com::Log ( L"SERVER", L"peer up   : %s", peer ); } );
    server.onPeerDown ( [] ( LPCWSTR peer ) { com::Log ( L"SERVER", L"peer down : %s", peer ); } );
    server.onError    ( [] ( LPCWSTR what ) { com::Log ( L"SERVER", L"error     : %s", what ); } );

    hr = server.listen ( kClientAddr, com::Dmx ( kServiceName ).c_str() );
    if ( FAILED(hr) ) return com::SetupFailure ( L"Listen(dmx)", hr );
    com::Log ( L"SERVER", L"Dmx service '%s' armed for '%s'", kServiceName, kClientAddr );

    // ---- CLIENT -------------------------------------------------------------
    hr = net.createHub ( kClientAddr, client );
    if ( FAILED(hr) ) return com::SetupFailure ( L"CreateHub(client)", hr );

    client.onPeerUp ( [&] ( LPCWSTR peer )
    {
        com::Log ( L"CLIENT", L"peer up   : %s - Dmx connection ready", peer );
        HRESULT hrSend = client.sendText ( kServerAddr, kTopic,
                                           L"Hello over Dmx, through COM - no OS handle anywhere!" );
        com::Log ( L"CLIENT", L"SendText  : %s", com::HrName ( hrSend ) );
    });
    client.onPeerDown ( [] ( LPCWSTR peer ) { com::Log ( L"CLIENT", L"peer down : %s", peer ); } );
    client.onError    ( [] ( LPCWSTR what ) { com::Log ( L"CLIENT", L"error     : %s", what ); } );

    hr = client.connect ( kServerAddr, com::Dmx ( kServiceName ).c_str() );
    if ( FAILED(hr) ) return com::SetupFailure ( L"Connect(dmx)", hr );
    com::Log ( L"CLIENT", L"dialled the Dmx service" );

    com::Log ( L"MAIN", L"waiting up to 10s for connect + delivery (pumping)..." );
    bool ok = gDone.wait ( 10000 );

    com::Log ( L"MAIN", L"shutdown begin" );
    return com::Verdict ( ok,
                          L"server received the client's message over Dmx",
                          L"no message delivered (handshake did not complete)" );
}

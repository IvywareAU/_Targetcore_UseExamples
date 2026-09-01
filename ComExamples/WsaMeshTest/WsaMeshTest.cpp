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
// WsaMeshTest.cpp  (COM -- TargetCom)
//
// SINGLE-PROCESS, TWO-HUB LOOPBACK-TCP connectivity probe, driven entirely
// through COM.
//
// Same question and same verdict as the other two trees. What is different here
// is not the messaging -- it is that nothing in this executable is linked
// against MSCS at all:
//
//   DirectExamples       links TargetCore.lib + Msgcore.lib, includes four
//                          kernel headers, needs MFC
//   FacadeExamples  links TargetFacade.lib, includes one header
//   ComExamples    links NOTHING. ole32/oleaut32/uuid and a type
//                          library. The coclass is found in the registry at
//                          run time.
//
// That is the property the COM layer exists for: a client that has no build-time
// relationship with the framework at all -- which is what makes VB, Office, WSH
// and .NET possible.
//
// This runs single-threaded-apartment on purpose, so every event below crosses
// an apartment boundary via TargetCom's GIT machinery before it reaches these
// lambdas. See ComHarness.h.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : the server hub received the client's message over loopback TCP.
//   3 = TIMEOUT : handshake or delivery did not complete in time.
//   1 = SETUP   : the COM server is not registered, or a hub could not be armed.

#include "ComHarness.h"

// A plain port again. It used to be LONG because that was the automation-safe
// type IP2PHubCom::Listen took; the endpoint is a BSTR now, so nothing about
// the COM boundary constrains how a harness spells its own constants.
static const unsigned short kTestPort   = 7801;
static const wchar_t* const kServerAddr = L"WsaMesh.Server";
static const wchar_t* const kClientAddr = L"WsaMesh.Client";
static const wchar_t* const kTopic      = L"mesh";

int main ( )
{
    com::InitConsole();
    wprintf ( L"=== WsaMeshTest (COM) - two hubs, one process, loopback TCP ===\n" );
    wprintf ( L"Port : %d (127.0.0.1)   apartment: STA\n\n", (int)kTestPort );
    fflush ( stdout );

    com::Apartment sta;
    if ( !sta.ok() ) return com::EXIT_SETUP;

    com::Network net;
    if ( !net.ok() ) return com::SetupFailure ( L"CoCreateInstance(TargetCom.P2PNetwork)", net.hr() );
    wprintf ( L"        %s\n\n", net.versionString().c_str() );

    com::Gate gDone;
    com::Hub  server, client;

    // ---- Hub A: SERVER ------------------------------------------------------
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

    hr = server.listen ( kClientAddr, com::TcpListen ( kTestPort ).c_str() );
    if ( FAILED(hr) ) return com::SetupFailure ( L"Listen", hr );
    com::Log ( L"SERVER", L"listening for '%s' on port %d", kClientAddr, (int)kTestPort );

    // ---- Hub B: CLIENT ------------------------------------------------------
    hr = net.createHub ( kClientAddr, client );
    if ( FAILED(hr) ) return com::SetupFailure ( L"CreateHub(client)", hr );

    client.onPeerUp ( [&] ( LPCWSTR peer )
    {
        com::Log ( L"CLIENT", L"peer up   : %s - loopback TCP connection ready", peer );
        HRESULT hrSend = client.sendText ( kServerAddr, kTopic,
                                           L"Hello over loopback TCP, through COM!" );
        com::Log ( L"CLIENT", L"SendText  : %s", com::HrName ( hrSend ) );
    });
    client.onPeerDown ( [] ( LPCWSTR peer ) { com::Log ( L"CLIENT", L"peer down : %s", peer ); } );
    client.onError    ( [] ( LPCWSTR what ) { com::Log ( L"CLIENT", L"error     : %s", what ); } );

    hr = client.connect ( kServerAddr, com::TcpDial ( L"127.0.0.1", kTestPort ).c_str() );
    if ( FAILED(hr) ) return com::SetupFailure ( L"Connect", hr );
    com::Log ( L"CLIENT", L"dialling 127.0.0.1:%d (retries until answered)", (int)kTestPort );

    // ---- Wait. This PUMPS: in an STA a marshalled event arrives through the
    //      message queue, so a plain WaitForSingleObject here would hang. ----
    com::Log ( L"MAIN", L"waiting up to 10s for connect + delivery (pumping)..." );
    bool ok = gDone.wait ( 10000 );

    com::Log ( L"MAIN", L"shutdown begin" );
    return com::Verdict ( ok,
                          L"server received the client's message over loopback TCP",
                          L"no message delivered (handshake did not complete)" );
}

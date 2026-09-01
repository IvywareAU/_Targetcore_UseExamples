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
// PipeMeshTest.cpp  (COM -- TargetCom)
//
// SINGLE-PROCESS, TWO-HUB named-pipe connectivity probe, over COM.
//
// ONE STRING changed from WsaMeshTest (COM) and nothing else -- the same
// relationship the other two trees have between these two harnesses, and it got
// smaller: the two harnesses used to differ by a verb pair, and now differ by
// the scheme in a BSTR.
//
// That is worth more to a COM caller than to a C++ one. The transport is no
// longer a choice of method, so it is no longer a choice a script has to make
// at authoring time -- a VBScript client can read "pipe://..." out of an INI
// file and pass it to the same Listen it would use for TCP.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : the server hub received the client's message over the pipe.
//   3 = TIMEOUT : handshake or delivery did not complete in time.
//   1 = SETUP   : the COM server is not registered, or a hub could not be armed.

#include "ComHarness.h"

static const wchar_t* const kPipeName   = L"\\\\.\\pipe\\P2PmeshProbeCom";
static const wchar_t* const kServerAddr = L"PipeMesh.Server";
static const wchar_t* const kClientAddr = L"PipeMesh.Client";
static const wchar_t* const kTopic      = L"mesh";

int main ( )
{
    com::InitConsole();
    wprintf ( L"=== PipeMeshTest (COM) - two hubs, one process, named pipe ===\n" );
    wprintf ( L"Pipe : %s   apartment: STA\n\n", kPipeName );
    fflush ( stdout );

    com::Apartment sta;
    if ( !sta.ok() ) return com::EXIT_SETUP;

    com::Network net;
    if ( !net.ok() ) return com::SetupFailure ( L"CoCreateInstance(TargetCom.P2PNetwork)", net.hr() );

    com::Gate gDone;
    com::Hub  server, client;

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

    hr = server.listen ( kClientAddr, com::Pipe ( kPipeName ).c_str() );
    if ( FAILED(hr) ) return com::SetupFailure ( L"Listen(pipe)", hr );
    com::Log ( L"SERVER", L"pipe armed for '%s'", kClientAddr );

    hr = net.createHub ( kClientAddr, client );
    if ( FAILED(hr) ) return com::SetupFailure ( L"CreateHub(client)", hr );

    client.onPeerUp ( [&] ( LPCWSTR peer )
    {
        com::Log ( L"CLIENT", L"peer up   : %s - pipe ready", peer );
        HRESULT hrSend = client.sendText ( kServerAddr, kTopic,
                                           L"Hello over a named pipe, through COM!" );
        com::Log ( L"CLIENT", L"SendText  : %s", com::HrName ( hrSend ) );
    });
    client.onPeerDown ( [] ( LPCWSTR peer ) { com::Log ( L"CLIENT", L"peer down : %s", peer ); } );
    client.onError    ( [] ( LPCWSTR what ) { com::Log ( L"CLIENT", L"error     : %s", what ); } );

    hr = client.connect ( kServerAddr, com::Pipe ( kPipeName ).c_str() );
    if ( FAILED(hr) ) return com::SetupFailure ( L"Connect(pipe)", hr );
    com::Log ( L"CLIENT", L"opening the pipe (retries until it exists)" );

    com::Log ( L"MAIN", L"waiting up to 10s for connect + delivery (pumping)..." );
    bool ok = gDone.wait ( 10000 );

    com::Log ( L"MAIN", L"shutdown begin" );
    return com::Verdict ( ok,
                          L"server received the client's message over the pipe",
                          L"no message delivered (handshake did not complete)" );
}

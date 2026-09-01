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
// PipeMeshTest.cpp  (Light -- TargetFacade)
//
// SINGLE-PROCESS, TWO-HUB named-pipe connectivity probe.
//
// Identical in every respect to WsaMeshTest (Light) except ONE STRING: the
// endpoint reads "pipe://..." instead of "tcp://...". Not a different method,
// not a different overload -- a different value. That is the whole diff, and
// it is the point. In the original tree the same change meant a different
// header (P2PeerConPipe.h), a different factory class, different factory
// arguments, and a different endpoint type threaded through PostP2PeerCon.
//
//   original: P2PeerConPipe::ServiceFactory(kClientAddr, kPipeName)
//             oServer.PostP2PeerCon(pSvcCon)
//   light   : server.listen(kClientAddr, L"pipe://" kPipeName)
//
// Which also means this harness could take its endpoint from argv and become
// the TCP one with no rebuild.
//
// The original also needed Sleep(750) between arming the pipe and opening it,
// because a raw ClientFactory con calls CreateFile(OPEN_EXISTING) exactly once.
// Facade pipe dials retry, so that race is gone.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : the server hub received the client's message over the pipe.
//   3 = TIMEOUT : handshake or delivery did not complete in time.
//   1 = SETUP   : the network or a hub could not be created / armed.

#include "TargetFacadeFn.hpp"
#include "LightHarness.h"

static const wchar_t* const kPipeName   = L"\\\\.\\pipe\\P2PmeshProbeLight";
static const wchar_t* const kServerAddr = L"PipeMesh.Server";
static const wchar_t* const kClientAddr = L"PipeMesh.Client";
static const wchar_t* const kTopic      = L"mesh";

int main ( )
{
    light::InitConsole();
    wprintf ( L"=== PipeMeshTest (Light) - two hubs, one process, named pipe ===\n" );
    wprintf ( L"Pipe : %s\n\n", kPipeName );
    fflush ( stdout );

    light::Gate gDone;

    try
    {
        p2pf::Network net;

        // ---- Hub A: SERVER (creates + listens on the named pipe) ------------
        p2pf::Hub server = net.createHub ( kServerAddr );
        server.onTopic ( kTopic, [&] ( const p2pf::Message& m )
        {
            light::LogMessage ( L"SERVER", L"message", m.source, m.text() );
            gDone.open();
        });
        server.onPeerUp   ( [] ( const wchar_t *peer ) { light::Log ( L"SERVER", L"peer up   : %s", peer ); } );
        server.onPeerDown ( [] ( const wchar_t *peer ) { light::Log ( L"SERVER", L"peer down : %s", peer ); } );
        server.onError    ( [] ( const wchar_t *what ) { light::Log ( L"SERVER", L"error     : %s", what ); } );

        HRESULT hr = server.listen ( kClientAddr, light::Pipe ( kPipeName ).c_str() );
        if ( FAILED(hr) )
        {
            light::Log ( L"SERVER", L"FATAL: pipe listen failed (%s)", light::HrName ( hr ) );
            return light::EXIT_SETUP;
        }
        light::Log ( L"SERVER", L"pipe armed for '%s'", kClientAddr );

        // ---- Hub B: CLIENT (opens the named pipe) ---------------------------
        p2pf::Hub client = net.createHub ( kClientAddr );
        client.onPeerUp ( [&] ( const wchar_t *peer )
        {
            light::Log ( L"CLIENT", L"peer up   : %s - pipe ready", peer );
            HRESULT hrSend = client.sendText ( kServerAddr, kTopic,
                                               L"Hello over a named pipe, in one process!" );
            light::Log ( L"CLIENT", L"sendText  : %s", light::HrName ( hrSend ) );
        });
        client.onPeerDown ( [] ( const wchar_t *peer ) { light::Log ( L"CLIENT", L"peer down : %s", peer ); } );
        client.onError    ( [] ( const wchar_t *what ) { light::Log ( L"CLIENT", L"error     : %s", what ); } );

        hr = client.connect ( kServerAddr, light::Pipe ( kPipeName ).c_str() );
        if ( FAILED(hr) )
        {
            light::Log ( L"CLIENT", L"FATAL: pipe connect failed (%s)", light::HrName ( hr ) );
            return light::EXIT_SETUP;
        }
        light::Log ( L"CLIENT", L"opening the pipe (retries until it exists)" );

        light::Log ( L"MAIN", L"waiting up to 10s for connect + delivery..." );
        bool ok = gDone.wait ( 10000 );

        light::Log ( L"MAIN", L"shutdown begin" );
        return light::Verdict ( ok,
                                L"server received the client's message over the pipe",
                                L"no message delivered (handshake did not complete)" );
    }
    catch ( const std::exception& e )
    {
        printf ( "FATAL: %s\n", e.what() );
        return light::EXIT_SETUP;
    }
}

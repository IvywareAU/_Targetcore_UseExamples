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
// AlexInterop.cpp  (Light -- TargetFacade)
//
// TWO-PROCESS loopback-TCP probe with a strict exit-code contract, meant to be
// driven by an orchestrator that launches both sides and asserts BOTH exit 0.
//
//   AlexInteropLight server [port]              -- listen, wait for the message
//   AlexInteropLight send   [ip] [port] [text]  -- connect, send one message
//
// WHAT IS LOST HERE, STATED PLAINLY. The original (DirectExamples\AlexInterop,
// alex_test.cpp) exists to be PORTABLE: it is the Linux port's Phase-3 exit
// criterion, "AlexTest green Linux<->Linux", and it goes out of its way to avoid
// Win32-isms -- no _setmode/_O_U16TEXT, no GetLocalTime, narrow printf only --
// so the same source compiles against the io_uring shim with g++.
//
// TargetFacade is a Windows DLL built on MFC. Rewriting this harness on top of
// it therefore FORFEITS the one property the original was built for. This file
// is here for completeness of the mapping, not as a replacement: if you are
// working the Linux port, use the original. What it does still show is how much
// smaller the two-process shape gets when the facade owns the lifecycle.
//
// (The narrow-print helper the original needed for portability is also gone --
// this build can just use wprintf, which is exactly the trade being made.)
//
// Verdict by EXIT CODE (both sides):
//   0 = SUCCESS : server -- received the client's message (proves the message
//                           crossed the process boundary)
//                 client -- peer came up and the message was sent
//   3 = TIMEOUT : the awaited event did not fire before the deadline
//   1 = SETUP   : startup / arming failure

#include "TargetFacadeFn.hpp"
#include "LightHarness.h"

#include <stdlib.h>
#include <string>

static const wchar_t* const kServerAddr = L"AlexTest.Server";
static const wchar_t* const kClientAddr = L"AlexTest.Client";
static const wchar_t* const kTopic      = L"chat";

static std::wstring Widen ( const char *sz )
{
    std::wstring w;
    for ( ; sz && *sz; ++sz ) w.push_back ( (wchar_t)(unsigned char)*sz );
    return w;
}

static void Usage ( const char *argv0 )
{
    printf ( "usage:\n  %s server [port]\n  %s send   [ip] [port] [text]\n", argv0, argv0 );
}

int main ( int argc, char *argv[] )
{
    light::InitConsole();

    bool           bServer = true;
    std::wstring   strIP   = L"127.0.0.1";
    unsigned short nPort   = 7811;
    std::wstring   strMsg  = L"Hello from AlexTest client (two processes)!";

    if ( argc >= 2 )
    {
        if ( _stricmp ( argv[1], "send" ) == 0 )
        {
            bServer = false;
            if ( argc >= 3 ) strIP  = Widen ( argv[2] );
            if ( argc >= 4 ) nPort  = (unsigned short)atoi ( argv[3] );
            if ( argc >= 5 ) strMsg = Widen ( argv[4] );
        }
        else if ( _stricmp ( argv[1], "server" ) == 0 )
        {
            if ( argc >= 3 ) nPort = (unsigned short)atoi ( argv[2] );
        }
        else { Usage ( argv[0] ); return light::EXIT_SETUP; }
    }

    wprintf ( L"=== AlexInterop (Light, two-process) - %s ===\n", bServer ? L"SERVER" : L"CLIENT" );
    wprintf ( L"Port : %d  IP : %s  pid : %lu\n\n",
              (int)nPort, bServer ? L"127.0.0.1(listen)" : strIP.c_str(),
              ::GetCurrentProcessId() );
    fflush ( stdout );

    light::Gate gDone;

    try
    {
        p2pf::Network net;
        p2pf::Hub     hub = net.createHub ( bServer ? kServerAddr : kClientAddr );

        hub.onPeerDown ( [] ( const wchar_t *peer ) { light::Log ( L"HUB", L"peer down : %s", peer ); } );
        hub.onError    ( [] ( const wchar_t *what ) { light::Log ( L"HUB", L"error     : %s", what ); } );

        if ( bServer )
        {
            hub.onMessage ( [&] ( const p2pf::Message& m )
            {
                light::LogMessage ( L"SERVER", m.broadcast ? L"broadcast" : L"message",
                                    m.source, m.text() );
                gDone.open();
            });
            hub.onPeerUp ( [] ( const wchar_t *peer ) { light::Log ( L"SERVER", L"peer up   : %s", peer ); } );

            HRESULT hr = hub.listen ( kClientAddr, light::TcpListen ( nPort ).c_str() );
            if ( FAILED(hr) )
            {
                light::Log ( L"SERVER", L"FATAL: listen failed (%s)", light::HrName ( hr ) );
                return light::EXIT_SETUP;
            }
            light::Log ( L"SERVER", L"listening on port %d, waiting up to 15s...", (int)nPort );

            bool ok = gDone.wait ( 15000 );
            light::Log ( L"MAIN", L"shutdown begin" );
            return light::Verdict ( ok,
                                    L"received the client's message across the process boundary",
                                    L"no message arrived" );
        }
        else
        {
            hub.onPeerUp ( [&] ( const wchar_t *peer )
            {
                light::Log ( L"CLIENT", L"peer up   : %s - TCP connection ready", peer );
                HRESULT hrSend = hub.sendText ( kServerAddr, kTopic, strMsg.c_str() );
                light::Log ( L"CLIENT", L"sendText  : %s", light::HrName ( hrSend ) );
                if ( SUCCEEDED(hrSend) ) gDone.open();
            });

            HRESULT hr = hub.connect ( kServerAddr,
                                       light::TcpDial ( strIP.c_str(), nPort ).c_str() );
            if ( FAILED(hr) )
            {
                light::Log ( L"CLIENT", L"FATAL: connect failed (%s)", light::HrName ( hr ) );
                return light::EXIT_SETUP;
            }
            light::Log ( L"CLIENT", L"dialling %s:%d, waiting up to 10s...", strIP.c_str(), (int)nPort );

            bool ok = gDone.wait ( 10000 );

            // Give the posted message time to reach the wire before teardown.
            if ( ok ) ::Sleep ( 1000 );

            light::Log ( L"MAIN", L"shutdown begin" );
            return light::Verdict ( ok, L"handshake completed and the message was sent",
                                        L"the server never came up" );
        }
    }
    catch ( const std::exception& e )
    {
        printf ( "FATAL: %s\n", e.what() );
        return light::EXIT_SETUP;
    }
}

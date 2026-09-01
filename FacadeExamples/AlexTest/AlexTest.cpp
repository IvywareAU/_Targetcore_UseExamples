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
// AlexTest.cpp  (Light -- TargetFacade)
//
// The original demo app: two PROCESSES, TCP, one message.
//
//   AlexTestLight                       -- server mode (listens on port 7777)
//   AlexTestLight send [ip] [message]   -- client mode (connects, sends once)
//
// Start the server first, then the client -- though with the facade that is
// advice rather than a requirement, because a facade dial retries until the
// far side answers. The original's client got exactly one attempt.
//
// ONE DELIBERATE CHANGE FROM THE ORIGINAL. The original server blocked on
// getchar(), which makes it impossible to run unattended: with stdin
// redirected, getchar() returns EOF immediately and the "server" exits before
// the client can reach it (the original carried a comment about exactly this).
// The Light server instead waits for the message with a timeout and reports the
// outcome as an exit code, so run_all.ps1 can drive both processes. Press Enter
// to stop it early.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : server -- received the client's message
//                 client -- peer came up and the message was sent
//   3 = TIMEOUT : the awaited event did not happen in time
//   1 = SETUP   : the network or the hub could not be created / armed

#include "TargetFacadeFn.hpp"
#include "LightHarness.h"

#include <string>

static const unsigned short kTestPort   = 7777;
static const wchar_t* const kServerAddr = L"AlexTest.Server";
static const wchar_t* const kClientAddr = L"AlexTest.Client";
static const wchar_t* const kTopic      = L"chat";

static std::wstring Widen ( const char *sz )
{
    std::wstring w;
    for ( ; sz && *sz; ++sz ) w.push_back ( (wchar_t)(unsigned char)*sz );
    return w;
}

int main ( int argc, char *argv[] )
{
    light::InitConsole();

    bool         bServer = true;
    std::wstring strIP   = L"127.0.0.1";
    std::wstring strMsg  = L"Hello from AlexTest client!";

    if ( argc >= 2 && _stricmp ( argv[1], "send" ) == 0 )
    {
        bServer = false;
        if ( argc >= 3 ) strIP  = Widen ( argv[2] );
        if ( argc >= 4 ) strMsg = Widen ( argv[3] );
    }

    wprintf ( L"=== AlexTest (Light) - P2P messaging demo ===\n" );
    wprintf ( L"Mode  : %s\n", bServer ? L"SERVER  (listen on port 7777)"
                                       : L"CLIENT  (send one message)" );
    if ( !bServer ) wprintf ( L"Target: %s:%d\n", strIP.c_str(), (int)kTestPort );
    wprintf ( L"\n" );
    fflush ( stdout );

    light::Gate gDone;

    try
    {
        // Startup and shutdown of the whole kernel, in one object, on every
        // exit path -- including the exceptional ones. The original repeated
        // CleanupP2Pmsg + WSACleanup + CloseHandle at six different returns.
        p2pf::Network net;

        p2pf::Hub hub = net.createHub ( bServer ? kServerAddr : kClientAddr );

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

            HRESULT hr = hub.listen ( kClientAddr, light::TcpListen ( kTestPort ).c_str() );
            if ( FAILED(hr) )
            {
                light::Log ( L"SERVER", L"FATAL: listen failed (%s)", light::HrName ( hr ) );
                return light::EXIT_SETUP;
            }
            // Wait for the message, and additionally for a keypress IF stdin is
            // a real console. That condition is the whole fix for the original's
            // trap: a redirected or closed stdin handle is permanently
            // signalled, so waiting on it unattended returns instantly and the
            // "server" dies before the client can reach it.
            HANDLE hIn   = ::GetStdHandle ( STD_INPUT_HANDLE );
            DWORD  dwMode = 0;
            bool   bConsoleIn = ( hIn != INVALID_HANDLE_VALUE ) &&
                                ( ::GetConsoleMode ( hIn, &dwMode ) != 0 );

            light::Log ( L"SERVER", L"listening on port %d - waiting up to 30s%s",
                         (int)kTestPort,
                         bConsoleIn ? L" (Enter to stop)" : L" (stdin not a console: timeout only)" );

            DWORD dw;
            if ( bConsoleIn )
            {
                HANDLE waits[2] = { gDone.raw(), hIn };
                dw = ::WaitForMultipleObjects ( 2, waits, FALSE, 30000 );
            }
            else
            {
                dw = ::WaitForSingleObject ( gDone.raw(), 30000 );
            }

            bool ok = gDone.isOpen();
            if ( bConsoleIn && dw == WAIT_OBJECT_0 + 1 && !ok )
                light::Log ( L"SERVER", L"stopped from the console before any message arrived" );

            light::Log ( L"MAIN", L"shutdown begin" );
            return light::Verdict ( ok, L"server received the client's message",
                                        L"no message arrived" );
        }
        else
        {
            hub.onPeerUp ( [&] ( const wchar_t *peer )
            {
                light::Log ( L"CLIENT", L"peer up   : %s - connection ready", peer );
                HRESULT hrSend = hub.sendText ( kServerAddr, kTopic, strMsg.c_str() );
                light::Log ( L"CLIENT", L"sendText  : %s", light::HrName ( hrSend ) );
                if ( SUCCEEDED(hrSend) ) gDone.open();
            });

            HRESULT hr = hub.connect ( kServerAddr,
                                       light::TcpDial ( strIP.c_str(), kTestPort ).c_str() );
            if ( FAILED(hr) )
            {
                light::Log ( L"CLIENT", L"FATAL: connect failed (%s)", light::HrName ( hr ) );
                return light::EXIT_SETUP;
            }
            light::Log ( L"CLIENT", L"connecting to %s:%d (retries until answered)",
                         strIP.c_str(), (int)kTestPort );

            bool ok = gDone.wait ( 10000 );

            // Let the posted message reach the wire before the hub goes down.
            if ( ok ) ::Sleep ( 500 );

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

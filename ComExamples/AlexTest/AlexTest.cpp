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
// AlexTest.cpp  (COM -- TargetCom)
//
// The original demo app: two PROCESSES, TCP, one message -- over COM.
//
//   AlexTestCom                       -- server mode (listens on port 7777)
//   AlexTestCom send [ip] [message]   -- client mode (connects, sends once)
//
// Two processes means two separate CoCreateInstance calls, each getting its own
// in-proc TargetCom, its own facade network and its own kernel. Nothing is
// shared but the socket -- which is the point of the exercise.
//
// Same deliberate change as the Light version: the original server blocked on
// getchar(), which cannot be run unattended (redirected stdin returns EOF
// immediately and the "server" exits before the client arrives). This one waits
// on the message, and additionally on a keypress ONLY when stdin is a real
// console. It still stops on Enter when run by hand.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : server -- received the client's message
//                 client -- peer came up and the message was sent
//   3 = TIMEOUT : the awaited event did not happen in time
//   1 = SETUP   : the COM server is not registered, or the hub could not be armed

#include "ComHarness.h"

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
    com::InitConsole();

    bool         bServer = true;
    std::wstring strIP   = L"127.0.0.1";
    std::wstring strMsg  = L"Hello from AlexTest client, through COM!";

    if ( argc >= 2 && _stricmp ( argv[1], "send" ) == 0 )
    {
        bServer = false;
        if ( argc >= 3 ) strIP  = Widen ( argv[2] );
        if ( argc >= 4 ) strMsg = Widen ( argv[3] );
    }

    wprintf ( L"=== AlexTest (COM) - P2P messaging demo ===\n" );
    wprintf ( L"Mode  : %s\n", bServer ? L"SERVER  (listen on port 7777)"
                                       : L"CLIENT  (send one message)" );
    if ( !bServer ) wprintf ( L"Target: %s:%d\n", strIP.c_str(), (int)kTestPort );
    wprintf ( L"\n" );
    fflush ( stdout );

    com::Apartment sta;
    if ( !sta.ok() ) return com::EXIT_SETUP;

    com::Network net;
    if ( !net.ok() ) return com::SetupFailure ( L"CoCreateInstance(TargetCom.P2PNetwork)", net.hr() );

    com::Gate gDone;
    com::Hub  hub;

    HRESULT hr = net.createHub ( bServer ? kServerAddr : kClientAddr, hub );
    if ( FAILED(hr) ) return com::SetupFailure ( L"CreateHub", hr );

    hub.onPeerDown ( [] ( LPCWSTR peer ) { com::Log ( L"HUB", L"peer down : %s", peer ); } );
    hub.onError    ( [] ( LPCWSTR what ) { com::Log ( L"HUB", L"error     : %s", what ); } );

    if ( bServer )
    {
        hub.onMessage ( [&] ( const com::Message& m )
        {
            com::LogMessage ( L"SERVER", m.broadcast ? L"broadcast" : L"message", m.source, m.text() );
            gDone.open();
        });
        hub.onPeerUp ( [] ( LPCWSTR peer ) { com::Log ( L"SERVER", L"peer up   : %s", peer ); } );

        hr = hub.listen ( kClientAddr, com::TcpListen ( kTestPort ).c_str() );
        if ( FAILED(hr) ) return com::SetupFailure ( L"Listen", hr );

        HANDLE hIn = ::GetStdHandle ( STD_INPUT_HANDLE );
        DWORD  dwMode = 0;
        bool   bConsoleIn = ( hIn != INVALID_HANDLE_VALUE ) &&
                            ( ::GetConsoleMode ( hIn, &dwMode ) != 0 );

        com::Log ( L"SERVER", L"listening on port %d - waiting up to 30s%s",
                   (int)kTestPort,
                   bConsoleIn ? L" (Enter to stop)" : L" (stdin not a console: timeout only)" );

        // Pump while waiting, and poll the console handle if there is one. The
        // pump is not optional: without it the marshalled OnMessage never
        // arrives in this apartment.
        DWORD start = ::GetTickCount();
        bool  ok    = false;
        for ( ;; )
        {
            com::Pump();
            if ( gDone.isOpen() ) { ok = true; break; }
            if ( bConsoleIn && ::WaitForSingleObject ( hIn, 0 ) == WAIT_OBJECT_0 )
            {
                com::Log ( L"SERVER", L"stopped from the console before any message arrived" );
                break;
            }
            if ( ::GetTickCount() - start > 30000 ) break;
            ::Sleep ( 5 );
        }

        com::Log ( L"MAIN", L"shutdown begin" );
        return com::Verdict ( ok, L"server received the client's message", L"no message arrived" );
    }
    else
    {
        hub.onPeerUp ( [&] ( LPCWSTR peer )
        {
            com::Log ( L"CLIENT", L"peer up   : %s - connection ready", peer );
            HRESULT h = hub.sendText ( kServerAddr, kTopic, strMsg.c_str() );
            com::Log ( L"CLIENT", L"SendText  : %s", com::HrName ( h ) );
            if ( SUCCEEDED(h) ) gDone.open();
        });

        hr = hub.connect ( kServerAddr, com::TcpDial ( strIP.c_str(), kTestPort ).c_str() );
        if ( FAILED(hr) ) return com::SetupFailure ( L"Connect", hr );
        com::Log ( L"CLIENT", L"connecting to %s:%d (retries until answered)",
                   strIP.c_str(), (int)kTestPort );

        bool ok = gDone.wait ( 10000 );
        if ( ok ) com::PumpFor ( 500 );      // let the message reach the wire

        com::Log ( L"MAIN", L"shutdown begin" );
        return com::Verdict ( ok, L"handshake completed and the message was sent",
                                  L"the server never came up" );
    }
}

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
// AlexInterop.cpp  (COM -- TargetCom)
//
// TWO-PROCESS loopback-TCP probe with a strict exit-code contract, over COM.
//
//   AlexInteropCom server [port]              -- listen, wait for the message
//   AlexInteropCom send   [ip] [port] [text]  -- connect, send one message
//
// WHAT IS LOST HERE -- worse than in the Light tree, and worth being blunt
// about. The original (DirectExamples\AlexInterop) exists to be PORTABLE: it
// is the Linux port's Phase-3 exit criterion, "AlexTest green Linux<->Linux",
// written to avoid every Win32-ism so the same source builds against the
// io_uring shim with g++.
//
// The facade rewrite already forfeited that by depending on a Windows MFC DLL.
// The COM rewrite forfeits it twice over: COM itself, the registry, apartments
// and BSTR/SAFEARRAY have no Linux counterpart at all. This file is here so the
// mapping is complete, and to show the two-process shape over COM. **If you are
// working the Linux port, use the original.**
//
// The interesting property it DOES have: two processes, two independent in-proc
// COM servers, two kernels, one socket between them.
//
// Verdict by EXIT CODE (both sides):
//   0 = SUCCESS  3 = TIMEOUT  1 = SETUP

#include "ComHarness.h"

#include <stdlib.h>

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

// This is the one harness whose port comes from argv, so it composes the
// endpoint from a LONG rather than going through com::TcpListen/TcpDial and
// their `unsigned short`. The distinction is not pedantry: "70000" narrowed to
// an unsigned short is 4464, a perfectly valid port nobody asked for, whereas
// the whole number handed to the parser comes back P2PF_E_ENDPOINT. The COM
// layer used to range-check its LONG parameter by hand for exactly this reason;
// the endpoint grammar now does it for every transport at once.
static std::wstring TcpListenArg ( LONG port )
{
    return L"tcp://:" + std::to_wstring ( port );
}
static std::wstring TcpDialArg ( const std::wstring& host, LONG port )
{
    return L"tcp://" + host + L":" + std::to_wstring ( port );
}

int main ( int argc, char *argv[] )
{
    com::InitConsole();

    bool         bServer = true;
    std::wstring strIP   = L"127.0.0.1";
    LONG         nPort   = 7811;
    std::wstring strMsg  = L"Hello from AlexTest client (two processes, COM)!";

    if ( argc >= 2 )
    {
        if ( _stricmp ( argv[1], "send" ) == 0 )
        {
            bServer = false;
            if ( argc >= 3 ) strIP  = Widen ( argv[2] );
            if ( argc >= 4 ) nPort  = atol ( argv[3] );
            if ( argc >= 5 ) strMsg = Widen ( argv[4] );
        }
        else if ( _stricmp ( argv[1], "server" ) == 0 )
        {
            if ( argc >= 3 ) nPort = atol ( argv[2] );
        }
        else { Usage ( argv[0] ); return com::EXIT_SETUP; }
    }

    wprintf ( L"=== AlexInterop (COM, two-process) - %s ===\n", bServer ? L"SERVER" : L"CLIENT" );
    wprintf ( L"Port : %d  IP : %s  pid : %lu\n\n",
              (int)nPort, bServer ? L"127.0.0.1(listen)" : strIP.c_str(), ::GetCurrentProcessId() );
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

        hr = hub.listen ( kClientAddr, TcpListenArg ( nPort ).c_str() );
        if ( FAILED(hr) ) return com::SetupFailure ( L"Listen", hr );
        com::Log ( L"SERVER", L"listening on port %d, waiting up to 15s (pumping)...", (int)nPort );

        bool ok = gDone.wait ( 15000 );
        com::Log ( L"MAIN", L"shutdown begin" );
        return com::Verdict ( ok,
                              L"received the client's message across the process boundary",
                              L"no message arrived" );
    }
    else
    {
        hub.onPeerUp ( [&] ( LPCWSTR peer )
        {
            com::Log ( L"CLIENT", L"peer up   : %s - TCP connection ready", peer );
            HRESULT h = hub.sendText ( kServerAddr, kTopic, strMsg.c_str() );
            com::Log ( L"CLIENT", L"SendText  : %s", com::HrName ( h ) );
            if ( SUCCEEDED(h) ) gDone.open();
        });

        hr = hub.connect ( kServerAddr, TcpDialArg ( strIP, nPort ).c_str() );
        if ( FAILED(hr) ) return com::SetupFailure ( L"Connect", hr );
        com::Log ( L"CLIENT", L"dialling %s:%d, waiting up to 10s (pumping)...", strIP.c_str(), (int)nPort );

        bool ok = gDone.wait ( 10000 );
        if ( ok ) com::PumpFor ( 1000 );

        com::Log ( L"MAIN", L"shutdown begin" );
        return com::Verdict ( ok, L"handshake completed and the message was sent",
                                  L"the server never came up" );
    }
}

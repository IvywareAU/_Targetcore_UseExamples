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
// Com232MeshTest.cpp  (COM -- TargetCom)
//
// SINGLE-PROCESS, TWO-HUB RS-232 serial connection probe, over COM.
//
// PREREQUISITE (unchanged from both other trees): a com0com virtual null-modem
// pair COM5<->COM6
//     setupc install PortName=COM5 PortName=COM6
// Without it this reports SETUP and exits 1.
//
// The port number used to cross as a LONG so that VT_I4 could carry it and the
// COM layer could range-check it before the facade saw a truncated `short`.
// There is no numeric parameter left to protect: the port is text inside
// "serial://COM5", and the check moved to the one place the grammar is parsed.
// A bad port is now P2PF_E_ENDPOINT out of the facade rather than E_INVALIDARG
// out of the COM layer -- the same mistake caught in the same call, by a rule
// that every one of the three trees now shares instead of just this one.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : the server hub received the client's message over the wire.
//   3 = TIMEOUT : handshake or delivery did not complete in time.
//   1 = SETUP   : no com0com pair, or the COM server is not registered.

#include "ComHarness.h"

static const unsigned short kServerComPort = 5;
static const unsigned short kClientComPort = 6;
static const wchar_t* const kServerAddr    = L"Com232Mesh.Server";
static const wchar_t* const kClientAddr    = L"Com232Mesh.Client";
static const wchar_t* const kTopic         = L"mesh";

int main ( )
{
    com::InitConsole();
    wprintf ( L"=== Com232MeshTest (COM) - two hubs, one process, RS-232 ===\n" );
    wprintf ( L"Server : COM%d   Client : COM%d   (com0com null-modem pair required)\n\n",
              (int)kServerComPort, (int)kClientComPort );
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

    // Armed first: a serial:// dial does not retry.
    hr = server.listen ( kClientAddr, com::Serial ( kServerComPort ).c_str() );
    if ( FAILED(hr) )
    {
        com::Log ( L"SERVER", L"SETUP: Listen(serial://COM%d) -> %s",
                   (int)kServerComPort, com::HrName ( hr ) );
        wprintf ( L"\nNo com0com pair on COM%d/COM%d? Install one with:\n"
                  L"    setupc install PortName=COM%d PortName=COM%d\n",
                  (int)kServerComPort, (int)kClientComPort,
                  (int)kServerComPort, (int)kClientComPort );
        fflush ( stdout );
        return com::EXIT_SETUP;
    }
    com::Log ( L"SERVER", L"COM%d armed for '%s'", (int)kServerComPort, kClientAddr );

    hr = net.createHub ( kClientAddr, client );
    if ( FAILED(hr) ) return com::SetupFailure ( L"CreateHub(client)", hr );

    client.onPeerUp ( [&] ( LPCWSTR peer )
    {
        com::Log ( L"CLIENT", L"peer up   : %s - serial link ready", peer );
        HRESULT hrSend = client.sendText ( kServerAddr, kTopic,
                                           L"Hello over RS-232, through COM!" );
        com::Log ( L"CLIENT", L"SendText  : %s", com::HrName ( hrSend ) );
    });
    client.onPeerDown ( [] ( LPCWSTR peer ) { com::Log ( L"CLIENT", L"peer down : %s", peer ); } );
    client.onError    ( [] ( LPCWSTR what ) { com::Log ( L"CLIENT", L"error     : %s", what ); } );

    hr = client.connect ( kServerAddr, com::Serial ( kClientComPort ).c_str() );
    if ( FAILED(hr) )
    {
        com::Log ( L"CLIENT", L"SETUP: Connect(serial://COM%d) -> %s",
                   (int)kClientComPort, com::HrName ( hr ) );
        return com::EXIT_SETUP;
    }
    com::Log ( L"CLIENT", L"dialled from COM%d", (int)kClientComPort );

    // The range check, now in the endpoint parser rather than the COM layer.
    // "serial://COM0" is a legal-looking string with an illegal port in it, and
    // the parser insists the whole segment be numeric AND in range -- so this is
    // rejected for the same reason "serial://COMx" would be.
    HRESULT hrBad = server.listen ( L"Com232Mesh.Nope", com::Serial ( 0 ).c_str() );
    com::Log ( L"MAIN", L"Listen(serial://COM0) -> %s (range-checked in the parser)",
               com::HrName ( hrBad ) );

    com::Log ( L"MAIN", L"waiting up to 15s for login + delivery (pumping)..." );
    bool ok = gDone.wait ( 15000 ) && ( hrBad == com::E_ENDPOINT );

    com::Log ( L"MAIN", L"shutdown begin" );
    return com::Verdict ( ok,
                          L"server received the client's message over RS-232",
                          L"no message delivered (handshake did not complete)" );
}

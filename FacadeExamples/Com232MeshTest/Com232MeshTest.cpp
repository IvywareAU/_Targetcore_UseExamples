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
// Com232MeshTest.cpp  (Light -- TargetFacade)
//
// SINGLE-PROCESS, TWO-HUB RS-232 serial connection probe.
//
// Fourth instance of the same shape, fourth endpoint scheme: "serial://COMn".
// The two hubs sit on the two ends of a com0com virtual null-modem pair,
// exactly as in the original.
//
// Note that each side names its OWN port, which is why this is the one
// transport IP2PNetwork::Link cannot express: a null-modem link is two
// DIFFERENT local devices, each opened exclusively, and Link carries one
// endpoint for both sides.
//
// PREREQUISITE (unchanged from the original): a com0com pair COM5<->COM6
//   setupc install PortName=COM5 PortName=COM6
// Without it this harness reports SETUP and exits 1 -- it is the one Light
// example that cannot run on a machine with no serial pair, which is also why
// the facade's serial path is the one path its own smoke test cannot cover.
//
// Ordering: a serial:// dial does NOT retry (a missing COM port is a
// configuration fault, not a timing one), so the listening end is armed first
// -- the same rule the original enforced with Sleep(1500), and for the same
// underlying reason: WaitCommEvent only reports events raised after
// SetCommMask, so login bytes arriving before the arm would go unnoticed.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : the server hub received the client's message over the wire.
//   3 = TIMEOUT : handshake or delivery did not complete in time.
//   1 = SETUP   : no com0com pair (or the ports are in use).

#include "TargetFacadeFn.hpp"
#include "LightHarness.h"

static const unsigned short kServerComPort = 5;    // server listens on COM5
static const unsigned short kClientComPort = 6;    // client dials from COM6
static const wchar_t* const kServerAddr    = L"Com232Mesh.Server";
static const wchar_t* const kClientAddr    = L"Com232Mesh.Client";
static const wchar_t* const kTopic         = L"mesh";

int main ( )
{
    light::InitConsole();
    wprintf ( L"=== Com232MeshTest (Light) - two hubs, one process, RS-232 ===\n" );
    wprintf ( L"Server : COM%d   Client : COM%d   (com0com null-modem pair required)\n\n",
              (int)kServerComPort, (int)kClientComPort );
    fflush ( stdout );

    light::Gate gDone;

    try
    {
        p2pf::Network net;

        // ---- Hub A: SERVER -- armed FIRST -----------------------------------
        p2pf::Hub server = net.createHub ( kServerAddr );
        server.onTopic ( kTopic, [&] ( const p2pf::Message& m )
        {
            light::LogMessage ( L"SERVER", L"message", m.source, m.text() );
            gDone.open();
        });
        server.onPeerUp   ( [] ( const wchar_t *peer ) { light::Log ( L"SERVER", L"peer up   : %s", peer ); } );
        server.onPeerDown ( [] ( const wchar_t *peer ) { light::Log ( L"SERVER", L"peer down : %s", peer ); } );
        server.onError    ( [] ( const wchar_t *what ) { light::Log ( L"SERVER", L"error     : %s", what ); } );

        HRESULT hr = server.listen ( kClientAddr, light::Serial ( kServerComPort ).c_str() );
        if ( FAILED(hr) )
        {
            light::Log ( L"SERVER", L"SETUP: listen(serial://COM%d) failed (%s)",
                         (int)kServerComPort, light::HrName ( hr ) );
            wprintf ( L"\nNo com0com pair on COM%d/COM%d? Install one with:\n"
                      L"    setupc install PortName=COM%d PortName=COM%d\n",
                      (int)kServerComPort, (int)kClientComPort,
                      (int)kServerComPort, (int)kClientComPort );
            fflush ( stdout );
            return light::EXIT_SETUP;
        }
        light::Log ( L"SERVER", L"COM%d armed for '%s'", (int)kServerComPort, kClientAddr );

        // ---- Hub B: CLIENT --------------------------------------------------
        p2pf::Hub client = net.createHub ( kClientAddr );
        client.onPeerUp ( [&] ( const wchar_t *peer )
        {
            light::Log ( L"CLIENT", L"peer up   : %s - serial link ready", peer );
            HRESULT hrSend = client.sendText ( kServerAddr, kTopic,
                                               L"Hello over RS-232, one hub at each end of the null modem!" );
            light::Log ( L"CLIENT", L"sendText  : %s", light::HrName ( hrSend ) );
        });
        client.onPeerDown ( [] ( const wchar_t *peer ) { light::Log ( L"CLIENT", L"peer down : %s", peer ); } );
        client.onError    ( [] ( const wchar_t *what ) { light::Log ( L"CLIENT", L"error     : %s", what ); } );

        hr = client.connect ( kServerAddr, light::Serial ( kClientComPort ).c_str() );
        if ( FAILED(hr) )
        {
            light::Log ( L"CLIENT", L"SETUP: connect(serial://COM%d) failed (%s)",
                         (int)kClientComPort, light::HrName ( hr ) );
            return light::EXIT_SETUP;
        }
        light::Log ( L"CLIENT", L"dialled from COM%d", (int)kClientComPort );

        light::Log ( L"MAIN", L"waiting up to 15s for login + delivery over the wire..." );
        bool ok = gDone.wait ( 15000 );

        light::Log ( L"MAIN", L"shutdown begin" );
        return light::Verdict ( ok,
                                L"server received the client's message over RS-232",
                                L"no message delivered (handshake did not complete)" );
    }
    catch ( const std::exception& e )
    {
        printf ( "FATAL: %s\n", e.what() );
        return light::EXIT_SETUP;
    }
}

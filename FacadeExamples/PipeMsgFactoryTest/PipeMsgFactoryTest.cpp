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
// PipeMsgFactoryTest.cpp  (Light -- TargetFacade)
//
// The same HubPing -> HubPong round trip as PipeMsgMapTest (Light), with the
// SEND SITE changed -- which is exactly the relationship the two originals had
// to each other.
//
// WHAT THE ORIGINAL WAS ABOUT, AND WHAT HAPPENED TO IT. The original existed to
// show that the framework's message FACTORIES produce something that routes
// identically to a hand-built P2PeerMsg32:
//
//     P2PeerMsg32 oSeed(kClientAddr, kClientAddr, kMsgPing, lpszMsg, nBytes);
//     P2PeerMsg*  pMsg = oSeed.RedirectFactory(kServerAddr, kMsgPing, lpszMsg, nBytes);
//     PostP2PeerMsg(pMsg);
//
// ...and it carried a long warning about why RedirectFactory and NOT
// ResponseFactory: ResponseFactory reverses the addressing but also inherits
// the received message's routing prefix, so standalone-posting one loops the
// reply back into the current hub's own map instead of sending it across the
// pipe.
//
// The facade has no factory family, so that entire class of decision is gone:
// there is ONE send verb, it takes a destination and a topic, and there is no
// envelope to inherit. This file therefore keeps what the original was really
// demonstrating -- a caller-constructed payload routed to a named handler --
// and drops the part that only existed to navigate the factory API:
//
//     hub.send(dest, topic, &blob, sizeof(blob));   // caller owns the bytes
//
// Note the ownership difference while you are here. The factory returned a
// heap message the caller owned and PostP2PeerMsg then took; a leak or a
// double-free lived in that handoff. send() copies the caller's bytes before
// it returns, so there is no handoff at all.
//
//   client --HubPing(binary struct)--> [pipe] --> server's onTopic(HubPing)
//   server --HubPong(binary struct)--> [pipe] --> client's onTopic(HubPong)
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : the client received the server's HubPong reply, and the
//                 struct came back byte-for-byte with its sequence incremented.
//   3 = TIMEOUT : the round trip did not complete in time.
//   1 = SETUP   : the network or a hub could not be created / armed.

#include "TargetFacadeFn.hpp"
#include "LightHarness.h"

#include <string.h>

static const wchar_t* const kPipeName   = L"\\\\.\\pipe\\P2PmsgFactoryProbeLight";
static const wchar_t* const kServerAddr = L"MsgFac.Server";
static const wchar_t* const kClientAddr = L"MsgFac.Client";

static const wchar_t* const kMsgPing = L"HubPing";
static const wchar_t* const kMsgPong = L"HubPong";

// The caller-built payload. Any POD travels as-is: the facade copies the bytes
// and hands the receiver a pointer valid for the callback's duration.
#pragma pack(push, 1)
struct Probe
{
    unsigned int magic;        // 0x50524F42 'PROB'
    unsigned int sequence;     // incremented by the responder
    wchar_t      note[48];
};
#pragma pack(pop)

static const unsigned int kProbeMagic = 0x50524F42;

int main ( )
{
    light::InitConsole();
    wprintf ( L"=== PipeMsgFactoryTest (Light) - caller-built payloads, one send verb ===\n" );
    wprintf ( L"Pipe   : %s\n", kPipeName );
    wprintf ( L"Payload: struct Probe (%d bytes), sent with send(), not sendText()\n\n",
              (int)sizeof(Probe) );
    fflush ( stdout );

    light::Gate gDone;
    bool        bReplyGood = false;

    try
    {
        p2pf::Network net;

        // ---- SERVER: receives the struct, bumps it, sends it back -----------
        p2pf::Hub server = net.createHub ( kServerAddr );

        server.onTopic ( kMsgPing, [&] ( const p2pf::Message& m )
        {
            if ( m.size != sizeof(Probe) )
            {
                light::Log ( L"SERVER", L"unexpected payload size %u", m.size );
                return;
            }

            Probe in;
            memcpy ( &in, m.payload, sizeof(in) );      // the pointer dies with this call
            wprintf ( L"\n[SERVER] onTopic('%s')  from='%s'  magic=%08X seq=%u\n  > %s\n\n",
                      m.topic, m.source, in.magic, in.sequence, in.note );
            fflush ( stdout );

            Probe out = in;
            out.sequence = in.sequence + 1;
            wcscpy_s ( out.note, L"Pong: server bumped your sequence." );

            HRESULT hr = server.send ( m.source, kMsgPong, &out, (unsigned int)sizeof(out) );
            light::Log ( L"SERVER", L"replied '%s' -> '%s' (seq %u) : %s",
                         kMsgPong, m.source, out.sequence, light::HrName ( hr ) );
        });
        server.onPeerUp ( [] ( const wchar_t *peer ) { light::Log ( L"SERVER", L"peer up : %s", peer ); } );
        server.onError  ( [] ( const wchar_t *what ) { light::Log ( L"SERVER", L"error   : %s", what ); } );

        HRESULT hr = server.listen ( kClientAddr, light::Pipe ( kPipeName ).c_str() );
        if ( FAILED(hr) )
        {
            light::Log ( L"SERVER", L"FATAL: pipe listen failed (%s)", light::HrName ( hr ) );
            return light::EXIT_SETUP;
        }

        // ---- CLIENT: sends the struct, verifies what comes back -------------
        p2pf::Hub client = net.createHub ( kClientAddr );

        client.onTopic ( kMsgPong, [&] ( const p2pf::Message& m )
        {
            Probe in;
            if ( m.size != sizeof(Probe) )
            {
                light::Log ( L"CLIENT", L"unexpected payload size %u", m.size );
                gDone.open();
                return;
            }
            memcpy ( &in, m.payload, sizeof(in) );
            wprintf ( L"\n[CLIENT] onTopic('%s')  from='%s'  magic=%08X seq=%u\n  > %s\n\n",
                      m.topic, m.source, in.magic, in.sequence, in.note );
            fflush ( stdout );

            bReplyGood = ( in.magic == kProbeMagic ) && ( in.sequence == 2 );
            gDone.open();
        });

        client.onPeerUp ( [&] ( const wchar_t *peer )
        {
            light::Log ( L"CLIENT", L"peer up : %s - pipe ready, sending ping", peer );

            Probe ping;
            memset ( &ping, 0, sizeof(ping) );
            ping.magic    = kProbeMagic;
            ping.sequence = 1;
            wcscpy_s ( ping.note, L"Ping: hello Server, this is Client." );

            HRESULT hrSend = client.send ( kServerAddr, kMsgPing, &ping, (unsigned int)sizeof(ping) );
            light::Log ( L"CLIENT", L"sent '%s' -> '%s' (seq %u) : %s",
                         kMsgPing, kServerAddr, ping.sequence, light::HrName ( hrSend ) );
        });
        client.onError ( [] ( const wchar_t *what ) { light::Log ( L"CLIENT", L"error   : %s", what ); } );

        hr = client.connect ( kServerAddr, light::Pipe ( kPipeName ).c_str() );
        if ( FAILED(hr) )
        {
            light::Log ( L"CLIENT", L"FATAL: pipe connect failed (%s)", light::HrName ( hr ) );
            return light::EXIT_SETUP;
        }

        light::Log ( L"MAIN", L"waiting up to 10s for the binary round trip..." );
        bool ok = gDone.wait ( 10000 ) && bReplyGood;

        light::Log ( L"MAIN", L"shutdown begin" );
        return light::Verdict ( ok,
                                L"the struct round-tripped with its sequence incremented",
                                L"no valid reply delivered (round trip did not complete)" );
    }
    catch ( const std::exception& e )
    {
        printf ( "FATAL: %s\n", e.what() );
        return light::EXIT_SETUP;
    }
}

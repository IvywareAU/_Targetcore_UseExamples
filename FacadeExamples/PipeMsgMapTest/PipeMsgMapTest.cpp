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
// PipeMsgMapTest.cpp  (Light -- TargetFacade)
//
// TWO HUBS, ONE PROCESS, NAMED PIPE, request/response routed BY MESSAGE NAME.
//
// This is the harness the facade was designed against, so it is the sharpest
// before/after in the set. The original demonstrated P2PeerMsg_MAP routing:
//
//     class PipeMapHub : public P2PeerHub
//     {
//         DECLARE_P2PeerMsg_MAP()
//         msgRESULT On_HubPing(P2PeerMsg* pMsg) { ... }
//         msgRESULT On_HubPong(P2PeerMsg* pMsg) { ... }
//     };
//     BEGIN_P2PeerMsg_MAP(PipeMapHub, P2PeerHub)
//         ON_P2PeerMsg(kMsgPing, On_HubPing)
//         ON_P2PeerMsg(kMsgPong, On_HubPong)
//     END_P2PeerMsg_MAP()
//
// The Light version is the same routing, minus the macros, minus the subclass:
//
//     server.onTopic(kMsgPing, [](const p2pf::Message& m) { ... });
//     client.onTopic(kMsgPong, [](const p2pf::Message& m) { ... });
//
// A facade "topic" IS the kernel message name -- dispatched by the kernel's own
// wildcard map machinery inside the facade, which is why the routing semantics
// are identical rather than merely similar. Names beginning with "P2Pmsg" stay
// kernel-reserved and are refused with P2PF_E_RESERVED_TOPIC; the last check
// below proves it.
//
//   client --HubPing--> [pipe] --> server's onTopic(HubPing)
//   server --HubPong--> [pipe] --> client's onTopic(HubPong)  => DONE
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : the client received the server's HubPong reply.
//   3 = TIMEOUT : the round trip did not complete in time.
//   1 = SETUP   : the network or a hub could not be created / armed.

#include "TargetFacadeFn.hpp"
#include "LightHarness.h"

static const wchar_t* const kPipeName   = L"\\\\.\\pipe\\P2PmsgMapProbeLight";
static const wchar_t* const kServerAddr = L"MsgMap.Server";
static const wchar_t* const kClientAddr = L"MsgMap.Client";

// Application-defined message names -- topics, in facade terms.
static const wchar_t* const kMsgPing = L"HubPing";
static const wchar_t* const kMsgPong = L"HubPong";

int main ( )
{
    light::InitConsole();
    wprintf ( L"=== PipeMsgMapTest (Light) - named-message routing, no macros ===\n" );
    wprintf ( L"Pipe   : %s\n", kPipeName );
    wprintf ( L"Server : %s\nClient : %s\n\n", kServerAddr, kClientAddr );
    fflush ( stdout );

    light::Gate gDone;

    try
    {
        p2pf::Network net;

        // ---- SERVER: intercepts HubPing, replies with HubPong ---------------
        p2pf::Hub server = net.createHub ( kServerAddr );

        server.onTopic ( kMsgPing, [&] ( const p2pf::Message& m )
        {
            wprintf ( L"\n[SERVER] onTopic('%s')  from='%s'\n  > %s\n\n",
                      m.topic, m.source, m.text() );
            fflush ( stdout );

            // The reply is addressed straight back at the sender. No factory,
            // no seed template, and none of the original's ResponseFactory
            // caveat about inheriting the request's routing prefix -- there is
            // no envelope to inherit, only a destination and a topic.
            HRESULT hr = server.sendText ( m.source, kMsgPong,
                                           L"Pong: server got your ping." );
            light::Log ( L"SERVER", L"replied '%s' -> '%s' : %s",
                         kMsgPong, m.source, light::HrName ( hr ) );
        });
        server.onPeerUp ( [] ( const wchar_t *peer ) { light::Log ( L"SERVER", L"peer up : %s", peer ); } );
        server.onError  ( [] ( const wchar_t *what ) { light::Log ( L"SERVER", L"error   : %s", what ); } );

        HRESULT hr = server.listen ( kClientAddr, light::Pipe ( kPipeName ).c_str() );
        if ( FAILED(hr) )
        {
            light::Log ( L"SERVER", L"FATAL: pipe listen failed (%s)", light::HrName ( hr ) );
            return light::EXIT_SETUP;
        }

        // ---- CLIENT: sends HubPing, intercepts HubPong ----------------------
        p2pf::Hub client = net.createHub ( kClientAddr );

        client.onTopic ( kMsgPong, [&] ( const p2pf::Message& m )
        {
            wprintf ( L"\n[CLIENT] onTopic('%s')  from='%s'\n  > %s\n\n",
                      m.topic, m.source, m.text() );
            fflush ( stdout );
            gDone.open();
        });

        // A message that matches no registered topic lands here instead of
        // being dropped -- the equivalent of falling through the map to
        // On_P2PeerUCast.
        client.onMessage ( [] ( const p2pf::Message& m )
        {
            light::Log ( L"CLIENT", L"unrouted topic '%s' from '%s'", m.topic, m.source );
        });

        client.onPeerUp ( [&] ( const wchar_t *peer )
        {
            light::Log ( L"CLIENT", L"peer up : %s - pipe ready, sending ping", peer );
            HRESULT hrSend = client.sendText ( kServerAddr, kMsgPing,
                                               L"Ping: hello Server, this is Client." );
            light::Log ( L"CLIENT", L"sent '%s' -> '%s' : %s",
                         kMsgPing, kServerAddr, light::HrName ( hrSend ) );
        });
        client.onError ( [] ( const wchar_t *what ) { light::Log ( L"CLIENT", L"error   : %s", what ); } );

        hr = client.connect ( kServerAddr, light::Pipe ( kPipeName ).c_str() );
        if ( FAILED(hr) )
        {
            light::Log ( L"CLIENT", L"FATAL: pipe connect failed (%s)", light::HrName ( hr ) );
            return light::EXIT_SETUP;
        }

        light::Log ( L"MAIN", L"waiting up to 10s for the HubPing -> HubPong round trip..." );
        bool ok = gDone.wait ( 10000 );

        // ---- The reserved-namespace rule, made visible ----------------------
        HRESULT hrReserved = client.sendText ( kServerAddr, L"P2Pmsg_Nope", L"x" );
        light::Log ( L"MAIN", L"topic 'P2Pmsg_Nope' -> %s (kernel-reserved namespace)",
                     light::HrName ( hrReserved ) );
        ok = ok && ( hrReserved == p2pf::P2PF_E_RESERVED_TOPIC );

        light::Log ( L"MAIN", L"shutdown begin" );
        return light::Verdict ( ok,
                                L"client received the server's HubPong reply",
                                L"no reply delivered (routing did not complete)" );
    }
    catch ( const std::exception& e )
    {
        printf ( "FATAL: %s\n", e.what() );
        return light::EXIT_SETUP;
    }
}

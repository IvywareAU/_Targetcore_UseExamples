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
// PipeMsgMapTest.cpp  (COM -- TargetCom)
//
// TWO HUBS, ONE PROCESS, NAMED PIPE, request/response routed BY MESSAGE NAME.
//
// The chain this harness completes is the reason the whole stack exists:
//
//   TargetCore   BEGIN_P2PeerMsg_MAP / ON_P2PeerMsg / END_P2PeerMsg_MAP
//                on a P2PeerHub subclass, plus DECLARE_P2PeerMsg_MAP
//   facade       hub.onTopic(name, lambda)          -- no macros, no subclass
//   COM          one _IP2PHubEvents.OnMessage event carrying (source, topic,
//                payload, broadcast), dispatched by topic on the client side
//
// The COM step is where the map stops being a language feature at all: there is
// exactly ONE event, and "routing by name" becomes an ordinary switch on the
// topic string -- which is what makes it expressible from VBScript, VBA or C#.
// ComHarness.h does that switch here, so this file still reads like a map.
//
//   client --HubPing--> [pipe] --> server's HubPing handler
//   server --HubPong--> [pipe] --> client's HubPong handler  => DONE
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : the client received the server's HubPong reply, AND the
//                 reserved-namespace rule was enforced.
//   3 = TIMEOUT : the round trip did not complete in time.
//   1 = SETUP   : the COM server is not registered, or a hub could not be armed.

#include "ComHarness.h"

static const wchar_t* const kPipeName   = L"\\\\.\\pipe\\P2PmsgMapProbeCom";
static const wchar_t* const kServerAddr = L"MsgMap.Server";
static const wchar_t* const kClientAddr = L"MsgMap.Client";

static const wchar_t* const kMsgPing = L"HubPing";
static const wchar_t* const kMsgPong = L"HubPong";

int main ( )
{
    com::InitConsole();
    wprintf ( L"=== PipeMsgMapTest (COM) - named-message routing over one event ===\n" );
    wprintf ( L"Pipe   : %s\n", kPipeName );
    wprintf ( L"Server : %s\nClient : %s\n\n", kServerAddr, kClientAddr );
    fflush ( stdout );

    com::Apartment sta;
    if ( !sta.ok() ) return com::EXIT_SETUP;

    com::Network net;
    if ( !net.ok() ) return com::SetupFailure ( L"CoCreateInstance(TargetCom.P2PNetwork)", net.hr() );

    com::Gate gDone;
    com::Hub  server, client;

    // ---- SERVER: intercepts HubPing, replies with HubPong -------------------
    HRESULT hr = net.createHub ( kServerAddr, server );
    if ( FAILED(hr) ) return com::SetupFailure ( L"CreateHub(server)", hr );

    server.onTopic ( kMsgPing, [&] ( const com::Message& m )
    {
        wprintf ( L"\n[SERVER] topic '%s'  from='%s'\n  > %s\n\n", m.topic, m.source, m.text() );
        fflush ( stdout );

        HRESULT h = server.sendText ( m.source, kMsgPong, L"Pong: server got your ping." );
        com::Log ( L"SERVER", L"replied '%s' -> '%s' : %s", kMsgPong, m.source, com::HrName ( h ) );
    });
    server.onPeerUp ( [] ( LPCWSTR peer ) { com::Log ( L"SERVER", L"peer up : %s", peer ); } );
    server.onError  ( [] ( LPCWSTR what ) { com::Log ( L"SERVER", L"error   : %s", what ); } );

    hr = server.listen ( kClientAddr, com::Pipe ( kPipeName ).c_str() );
    if ( FAILED(hr) ) return com::SetupFailure ( L"Listen(pipe)", hr );

    // ---- CLIENT: sends HubPing, intercepts HubPong --------------------------
    hr = net.createHub ( kClientAddr, client );
    if ( FAILED(hr) ) return com::SetupFailure ( L"CreateHub(client)", hr );

    client.onTopic ( kMsgPong, [&] ( const com::Message& m )
    {
        wprintf ( L"\n[CLIENT] topic '%s'  from='%s'\n  > %s\n\n", m.topic, m.source, m.text() );
        fflush ( stdout );
        gDone.open();
    });

    // Anything with no registered topic lands here rather than being dropped --
    // the equivalent of falling through the map to On_P2PeerUCast.
    client.onMessage ( [] ( const com::Message& m )
    {
        com::Log ( L"CLIENT", L"unrouted topic '%s' from '%s'", m.topic, m.source );
    });

    client.onPeerUp ( [&] ( LPCWSTR peer )
    {
        com::Log ( L"CLIENT", L"peer up : %s - pipe ready, sending ping", peer );
        HRESULT h = client.sendText ( kServerAddr, kMsgPing, L"Ping: hello Server, this is Client." );
        com::Log ( L"CLIENT", L"sent '%s' -> '%s' : %s", kMsgPing, kServerAddr, com::HrName ( h ) );
    });
    client.onError ( [] ( LPCWSTR what ) { com::Log ( L"CLIENT", L"error   : %s", what ); } );

    hr = client.connect ( kServerAddr, com::Pipe ( kPipeName ).c_str() );
    if ( FAILED(hr) ) return com::SetupFailure ( L"Connect(pipe)", hr );

    com::Log ( L"MAIN", L"waiting up to 10s for the HubPing -> HubPong round trip (pumping)..." );
    bool ok = gDone.wait ( 10000 );

    // ---- The reserved namespace, as an HRESULT a script can catch -----------
    HRESULT hrReserved = client.sendText ( kServerAddr, L"P2Pmsg_Nope", L"x" );
    com::Log ( L"MAIN", L"topic 'P2Pmsg_Nope' -> %s (kernel-reserved namespace)",
               com::HrName ( hrReserved ) );
    ok = ok && ( hrReserved == com::E_RESERVED_TOPIC );

    com::Log ( L"MAIN", L"shutdown begin" );
    return com::Verdict ( ok,
                          L"client received the server's HubPong reply",
                          L"no reply delivered (routing did not complete)" );
}

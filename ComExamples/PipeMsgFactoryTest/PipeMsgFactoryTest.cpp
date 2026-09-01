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
// PipeMsgFactoryTest.cpp  (COM -- TargetCom)
//
// The same HubPing -> HubPong round trip as PipeMsgMapTest (COM), with the SEND
// SITE changed -- a caller-built BINARY payload instead of text.
//
// WHERE THE ORIGINAL WENT. The original demonstrated P2PeerMsg::RedirectFactory
// (and warned at length why not ResponseFactory, which inherits the request's
// routing prefix and loops the reply back into the sender's own map). The
// facade replaced that whole family with one send verb. COM narrows it once
// more, and in an interesting way:
//
//     hub.Send(dest, topic, payload)      where payload is a VARIANT
//
// The VARIANT is the point. It accepts a SAFEARRAY(VT_UI1) -- what this file
// sends -- but ALSO a plain string, so a scripting client that has no way to
// build a byte array can still send. Anything else comes back
// DISP_E_TYPEMISMATCH, which the last check proves. A C++ caller building a
// struct and a VBScript caller passing a string reach the same Send.
//
// Ownership, for comparison: the factory returned a heap message the caller
// owned and PostP2PeerMsg then took -- a leak or a double-free lived in that
// handoff. Here the SAFEARRAY is copied into the message before Send returns
// and freed by VariantClear on the way out.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : the struct round-tripped with its sequence incremented, and a
//                 bad payload type was refused.
//   3 = TIMEOUT : the round trip did not complete in time.
//   1 = SETUP   : the COM server is not registered, or a hub could not be armed.

#include "ComHarness.h"

static const wchar_t* const kPipeName   = L"\\\\.\\pipe\\P2PmsgFactoryProbeCom";
static const wchar_t* const kServerAddr = L"MsgFac.Server";
static const wchar_t* const kClientAddr = L"MsgFac.Client";

static const wchar_t* const kMsgPing = L"HubPing";
static const wchar_t* const kMsgPong = L"HubPong";

#pragma pack(push, 1)
struct Probe
{
    unsigned int magic;        // 'PROB'
    unsigned int sequence;     // incremented by the responder
    wchar_t      note[48];
};
#pragma pack(pop)

static const unsigned int kProbeMagic = 0x50524F42;

int main ( )
{
    com::InitConsole();
    wprintf ( L"=== PipeMsgFactoryTest (COM) - binary payloads through a VARIANT ===\n" );
    wprintf ( L"Pipe   : %s\n", kPipeName );
    wprintf ( L"Payload: struct Probe (%d bytes) as SAFEARRAY(VT_UI1)\n\n", (int)sizeof(Probe) );
    fflush ( stdout );

    com::Apartment sta;
    if ( !sta.ok() ) return com::EXIT_SETUP;

    com::Network net;
    if ( !net.ok() ) return com::SetupFailure ( L"CoCreateInstance(TargetCom.P2PNetwork)", net.hr() );
    wprintf ( L"        MaxPayload = %ld bytes\n\n", net.maxPayload() );

    com::Gate gDone;
    com::Hub  server, client;
    bool      bReplyGood = false;

    // ---- SERVER: receives the struct, bumps it, sends it back ---------------
    HRESULT hr = net.createHub ( kServerAddr, server );
    if ( FAILED(hr) ) return com::SetupFailure ( L"CreateHub(server)", hr );

    server.onTopic ( kMsgPing, [&] ( const com::Message& m )
    {
        if ( m.size != sizeof(Probe) )
        {
            com::Log ( L"SERVER", L"unexpected payload size %u", m.size );
            return;
        }

        Probe in;
        memcpy ( &in, m.payload, sizeof(in) );
        wprintf ( L"\n[SERVER] topic '%s'  from='%s'  magic=%08X seq=%u\n  > %s\n\n",
                  m.topic, m.source, in.magic, in.sequence, in.note );
        fflush ( stdout );

        Probe out = in;
        out.sequence = in.sequence + 1;
        wcscpy_s ( out.note, L"Pong: server bumped your sequence." );

        HRESULT h = server.send ( m.source, kMsgPong, &out, (unsigned int)sizeof(out) );
        com::Log ( L"SERVER", L"replied '%s' -> '%s' (seq %u) : %s",
                   kMsgPong, m.source, out.sequence, com::HrName ( h ) );
    });
    server.onPeerUp ( [] ( LPCWSTR peer ) { com::Log ( L"SERVER", L"peer up : %s", peer ); } );
    server.onError  ( [] ( LPCWSTR what ) { com::Log ( L"SERVER", L"error   : %s", what ); } );

    hr = server.listen ( kClientAddr, com::Pipe ( kPipeName ).c_str() );
    if ( FAILED(hr) ) return com::SetupFailure ( L"Listen(pipe)", hr );

    // ---- CLIENT: sends the struct, verifies what comes back -----------------
    hr = net.createHub ( kClientAddr, client );
    if ( FAILED(hr) ) return com::SetupFailure ( L"CreateHub(client)", hr );

    client.onTopic ( kMsgPong, [&] ( const com::Message& m )
    {
        if ( m.size != sizeof(Probe) )
        {
            com::Log ( L"CLIENT", L"unexpected payload size %u", m.size );
            gDone.open();
            return;
        }
        Probe in;
        memcpy ( &in, m.payload, sizeof(in) );
        wprintf ( L"\n[CLIENT] topic '%s'  from='%s'  magic=%08X seq=%u\n  > %s\n\n",
                  m.topic, m.source, in.magic, in.sequence, in.note );
        fflush ( stdout );

        bReplyGood = ( in.magic == kProbeMagic ) && ( in.sequence == 2 );
        gDone.open();
    });

    client.onPeerUp ( [&] ( LPCWSTR peer )
    {
        com::Log ( L"CLIENT", L"peer up : %s - pipe ready, sending ping", peer );

        Probe ping;
        memset ( &ping, 0, sizeof(ping) );
        ping.magic    = kProbeMagic;
        ping.sequence = 1;
        wcscpy_s ( ping.note, L"Ping: hello Server, this is Client." );

        HRESULT h = client.send ( kServerAddr, kMsgPing, &ping, (unsigned int)sizeof(ping) );
        com::Log ( L"CLIENT", L"sent '%s' -> '%s' (seq %u) : %s",
                   kMsgPing, kServerAddr, ping.sequence, com::HrName ( h ) );
    });
    client.onError ( [] ( LPCWSTR what ) { com::Log ( L"CLIENT", L"error   : %s", what ); } );

    hr = client.connect ( kServerAddr, com::Pipe ( kPipeName ).c_str() );
    if ( FAILED(hr) ) return com::SetupFailure ( L"Connect(pipe)", hr );

    com::Log ( L"MAIN", L"waiting up to 10s for the binary round trip (pumping)..." );
    bool ok = gDone.wait ( 10000 ) && bReplyGood;

    // ---- What the VARIANT will and will not accept --------------------------
    VARIANT vBad;
    ::VariantInit ( &vBad );
    vBad.vt   = VT_I4;
    vBad.lVal = 42;
    HRESULT hrBad = client.raw()->Send ( com::Bstr(kServerAddr), com::Bstr(kMsgPing), vBad );
    com::Log ( L"MAIN", L"Send(payload = VT_I4) -> %s", com::HrName ( hrBad ) );
    ok = ok && ( hrBad == DISP_E_TYPEMISMATCH );

    com::Log ( L"MAIN", L"shutdown begin" );
    return com::Verdict ( ok,
                          L"the struct round-tripped with its sequence incremented",
                          L"no valid reply delivered (round trip did not complete)" );
}

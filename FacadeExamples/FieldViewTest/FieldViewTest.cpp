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
// FieldViewTest.cpp  (Light -- TargetFacade)
//
// `msg->fieldName = value` on the facade (MsgFieldAccessPlan.md, F4). No
// original in DirectExamples: this is new surface, and the harness exists to
// show it working end to end, over a real link, rather than only on a message
// that never leaves the process.
//
//   PART A (local)  : an OutMessage written through a P2PF_FIELD view and the
//                     dynamic form, read straight back -- every type, plus the
//                     error contract: a reserved name, an absent field, a read
//                     of the wrong size.
//   PART B (link)   : HubB sends that message to HubA over an in-process Dmx
//                     link. HubA's handler reads it through the SAME view type,
//                     confirms a write through a received message is refused,
//                     and answers with fields of its own; HubB reads the answer.
//
// B dials and so speaks first; A answers on receipt -- the ordering rule
// LocalInMemoryTest (Light) documents.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : every check passed and both legs were delivered.
//   3 = FAIL    : a check failed, or a delivery never arrived.
//   1 = SETUP   : the network or a hub could not be created / armed.

#include "TargetFacadeFn.hpp"
#include "LightHarness.h"

#include <atomic>

static const wchar_t* const kServiceName = L"P2PfieldViewLight";
static const wchar_t* const kAddrHubA    = L"FieldView.HubA";
static const wchar_t* const kAddrHubB    = L"FieldView.HubB";

struct Telemetry : p2pf::FieldView
{
    P2PF_FIELD ( device,  std::wstring );
    P2PF_FIELD ( uptime,  int );
    P2PF_FIELD ( serial,  long long );
    P2PF_FIELD ( ratio,   double );
    P2PF_FIELD ( online,  bool );
    P2PF_FIELD ( samples, p2pf::Blob );
};

struct Ack : p2pf::FieldView
{
    P2PF_FIELD ( seen,   int );
    P2PF_FIELD ( echoed, std::wstring );
};

static std::atomic<int> g_checks ( 0 ), g_failures ( 0 );

static void Check ( bool ok, LPCWSTR role, LPCWSTR what )
{
    ++g_checks;
    if ( !ok ) ++g_failures;
    light::Log ( role, L"%s  %s", ok ? L"ok  " : L"FAIL", what );
}

// The p2pf::FieldError a throwing statement raised, or S_OK if it did not.
template <class Fn>
static HRESULT ErrorOf ( Fn fn )
{
    try { fn(); return S_OK; }
    catch ( const p2pf::FieldError& e ) { return e.hr(); }
}

static const unsigned char kRaw[] = { 0, 1, 2, 0xFF, 0x80 };

static void Fill ( p2pf::OutMessage& out )
{
    p2pf::ViewOf<Telemetry> t ( out );
    t->device  = L"sensor-04";
    t->uptime  = 86400;
    t->serial  = 9000000000LL;
    t->ratio   = 0.75;
    t->online  = true;
    t->samples = p2pf::Blob ( kRaw, sizeof kRaw );
    out[L"note"] = "UTF-8 \xE2\x82\xAC";                // the dynamic form, narrow
}

// Reads back through ANY view -- the same function serves an OutMessage and a
// received Message, which is the point of one view type for both.
static void Verify ( const p2pf::ViewOf<Telemetry>& t, LPCWSTR role )
{
    int up = t->uptime;
    Check ( t->device.get() == L"sensor-04",          role, L"device   (text)" );
    Check ( up == 86400,                              role, L"uptime   (int)" );
    Check ( t->serial.get() == 9000000000LL,          role, L"serial   (long long)" );
    Check ( t->ratio.get() == 0.75,                   role, L"ratio    (double)" );
    Check ( t->online.get(),                          role, L"online   (bool)" );
    Check ( t->samples.get() == p2pf::Blob ( kRaw, sizeof kRaw ), role, L"samples  (blob)" );
    Check ( t[L"note"].asText() == L"UTF-8 \x20AC",   role, L"note     (UTF-8 in, UTF-16 out)" );
}

int main ( )
{
    light::InitConsole();
    wprintf ( L"=== FieldViewTest (Light) - msg->field = value on the facade ===\n\n" );
    fflush ( stdout );

    light::Gate gAtA, gAtB;

    try
    {
        p2pf::Network net;

        // ---- PART A : local ------------------------------------------------
        wprintf ( L"--- PART A: a view over an OutMessage ---\n" );
        fflush ( stdout );

        p2pf::OutMessage out = net.createMessage();
        Fill ( out );
        {
            p2pf::ViewOf<Telemetry> t ( out );
            Verify ( t, L"LOCAL" );
        }
        Check ( out.count() == 7, L"LOCAL", L"seven fields set" );
        // What a plain flat-ABI reader sees: SetFieldText's shape for text.
        {
            unsigned int cb = 0;
            out.raw()->GetField ( L"device", nullptr, &cb );
            Check ( cb == ( 9 + 1 ) * sizeof(wchar_t ), L"LOCAL", L"text is stored with its terminator" );
            out.raw()->GetField ( L"uptime", nullptr, &cb );
            Check ( cb == 4, L"LOCAL", L"an int is 4 bytes" );
        }
        Check ( ErrorOf ( [&]{ out[L"P2PFsecret"] = 1; } ) == p2pf::P2PF_E_RESERVED_TOPIC
              , L"LOCAL", L"the P2PF prefix is refused, by name" );
        Check ( ErrorOf ( [&]{ (void)out[L"absent"].asInt(); } ) == p2pf::P2PF_E_NO_FIELD
              , L"LOCAL", L"an absent field is P2PF_E_NO_FIELD" );
        Check ( !out[L"absent"].exists(), L"LOCAL", L"exists() tells absent from present" );
        Check ( ErrorOf ( [&]{ (void)out[L"uptime"].asReal(); } ) == E_INVALIDARG
              , L"LOCAL", L"4 bytes is not a double" );
        out[L"scratch"] = 1;
        Check ( out[L"scratch"].erase() && !out[L"scratch"].exists(), L"LOCAL", L"erase()" );

        // ---- PART B : over a link -------------------------------------------
        wprintf ( L"\n--- PART B: the same view at the far end of a Dmx link ---\n" );
        fflush ( stdout );

        p2pf::Hub hubA = net.createHub ( kAddrHubA );
        hubA.onTopic ( L"telemetry", [&] ( const p2pf::Message& m )
        {
            p2pf::ViewOf<Telemetry> t ( m );
            Verify ( t, L"HubA" );
            Check ( ErrorOf ( [&]{ t->uptime = 1; } ) == E_ACCESSDENIED
                  , L"HubA", L"a received message is read-only" );

            p2pf::OutMessage ack = net.createMessage();
            p2pf::ViewOf<Ack> a ( ack );
            a->seen   = (int)m.fieldNames().size();
            a->echoed = t->device.get();
            HRESULT h = hubA.replyMsg ( m, L"ack", ack );
            light::Log ( L"HubA", L"reply    : %s", light::HrName ( h ) );
            gAtA.open();
        });
        hubA.onError ( [] ( const wchar_t *what ) { light::Log ( L"HubA", L"error : %s", what ); } );

        HRESULT hr = hubA.listen ( kAddrHubB, light::Dmx ( kServiceName ).c_str() );
        if ( FAILED(hr) )
        {
            light::Log ( L"HubA", L"FATAL: Dmx listen failed (%s)", light::HrName ( hr ) );
            return light::EXIT_SETUP;
        }

        p2pf::Hub hubB = net.createHub ( kAddrHubB );
        hubB.onTopic ( L"ack", [&] ( const p2pf::Message& m )
        {
            p2pf::ViewOf<Ack> a ( m );
            Check ( a->seen.get() == 7,               L"HubB", L"HubA saw seven fields" );
            Check ( a->echoed.get() == L"sensor-04",  L"HubB", L"HubA echoed the device" );
            gAtB.open();
        });
        hubB.onError ( [] ( const wchar_t *what ) { light::Log ( L"HubB", L"error : %s", what ); } );
        hubB.onPeerUp ( [&] ( const wchar_t *peer )
        {
            light::Log ( L"HubB", L"peer up  : %s", peer );
            HRESULT h = hubB.sendMsg ( kAddrHubA, L"telemetry", out );
            light::Log ( L"HubB", L"B -> A   : %s", light::HrName ( h ) );
        });

        hr = hubB.connect ( kAddrHubA, light::Dmx ( kServiceName ).c_str() );
        if ( FAILED(hr) )
        {
            light::Log ( L"HubB", L"FATAL: Dmx connect failed (%s)", light::HrName ( hr ) );
            return light::EXIT_SETUP;
        }

        light::Gate *both[2] = { &gAtA, &gAtB };
        bool delivered = light::WaitAll ( both, 2, 10000 );
        Check ( delivered, L"MAIN", L"both legs delivered" );

        light::Log ( L"MAIN", L"%d checks, %d failed", g_checks.load(), g_failures.load() );
        light::Log ( L"MAIN", L"shutdown begin" );
        return light::Verdict ( delivered && g_failures == 0,
                                L"fields written through a view arrived and read back typed",
                                L"a check failed or a delivery did not arrive" );
    }
    catch ( const p2pf::FieldError& e )
    {
        printf ( "FATAL: unexpected FieldError 0x%08lX: %s\n", (unsigned long)e.hr(), e.what() );
        return light::EXIT_TIMEOUT;
    }
    catch ( const std::exception& e )
    {
        printf ( "FATAL: %s\n", e.what() );
        return light::EXIT_SETUP;
    }
}

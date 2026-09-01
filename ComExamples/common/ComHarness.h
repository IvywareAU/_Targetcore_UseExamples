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
// ComHarness.h
//
// The client-side layer that makes a raw COM client of TargetCom as pleasant to
// write as a facade client -- and, deliberately, uses NOTHING but plain COM to
// do it. No ATL, no _com_ptr_t, no #import, no MFC: just CoCreateInstance,
// vtable calls, a hand-written IDispatch sink and SysAllocString. If the layer
// needed a framework to be usable, that would be worth knowing; it does not.
//
// This is the COM counterpart of the facade's TargetFacadeFn.hpp, and the
// parallel is exact:
//
//   p2pf::Network net;                       com::Network net;
//   auto hub = net.createHub(L"A");          com::Hub hub(net, L"A");
//   hub.onTopic(L"chat", lambda);            hub.onTopic(L"chat", lambda);
//   hub.listen(peer, endpoint);              hub.listen(peer, endpoint);
//   hub.sendText(dst, topic, text);          hub.sendText(dst, topic, text);
//
// Underneath, every line of the right-hand column is doing more work:
// BSTR allocation, a VARIANT-wrapped SAFEARRAY for payloads, an
// IConnectionPointContainer::FindConnectionPoint + Advise for the events, and
// an apartment transition on every callback.
//
// APARTMENT CHOICE. Every harness runs single-threaded-apartment on purpose.
// STA is what the layer's real audience uses (VB, Office, WSH, most .NET UI
// hosts) and it is the case TargetCom's Global-Interface-Table machinery exists
// for: hub events are raised on a dispatch thread inside the DLL and have to be
// marshalled into this thread. The cost is that an STA MUST PUMP MESSAGES or
// nothing is ever delivered -- so com::Gate::wait() pumps while it waits, and
// every callback below therefore runs on the main thread.
//
// Exit-code contract, identical to DirectExamples and FacadeExamples:
//   0 = SUCCESS   1 = SETUP   3 = TIMEOUT / expectation not met

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <olectl.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "TargetCom_h.h"

// The GUID definitions (CLSID_P2PNetwork, IID_IP2PHubCom, DIID__IP2PHubEvents,
// ...). Every harness here is a single-translation-unit program, so defining
// them from the header keeps the harnesses free of COM boilerplate. Define
// COMHARNESS_NO_GUIDS in any additional .cpp that includes this header.
#ifndef COMHARNESS_NO_GUIDS
  #include "TargetCom_i.c"
#endif

namespace com {

const int EXIT_SUCCESS_ = 0;
const int EXIT_SETUP    = 1;
const int EXIT_TIMEOUT  = 3;

// The facade HRESULTs, which TargetCom passes through untouched. The IDL
// publishes the same values as the P2PFError enum, so a type-library client
// gets them as named constants; a raw vtable client spells them out here.
//
// S_UNRELATED_LINK leads the list because it is the one that is NOT an error:
// severity 0, so SUCCEEDED() is true and FAILED() is false. Every X.Server /
// X.Client pair in this tree is a SIBLING pair, and the surviving arming verbs
// classify the pair where the eight typed ones did not -- so this is the normal
// answer here, and a harness that tested `hr != S_OK` instead of FAILED(hr)
// would now fail on a working link.
const HRESULT S_UNRELATED_LINK = MAKE_HRESULT(0, FACILITY_ITF, 0x020C);

const HRESULT E_CON_FACTORY    = MAKE_HRESULT(1, FACILITY_ITF, 0x0203);
const HRESULT E_CON_DUPLICATE  = MAKE_HRESULT(1, FACILITY_ITF, 0x0204);
const HRESULT E_RESERVED_TOPIC = MAKE_HRESULT(1, FACILITY_ITF, 0x0205);
const HRESULT E_CLOSED         = MAKE_HRESULT(1, FACILITY_ITF, 0x0206);
const HRESULT E_HUB_DUPLICATE  = MAKE_HRESULT(1, FACILITY_ITF, 0x0207);
const HRESULT E_ENDPOINT       = MAKE_HRESULT(1, FACILITY_ITF, 0x0208);
const HRESULT E_UNRESOLVED     = MAKE_HRESULT(1, FACILITY_ITF, 0x0209);
const HRESULT E_NO_HUB         = MAKE_HRESULT(1, FACILITY_ITF, 0x020A);
const HRESULT E_LINK_PARTIAL   = MAKE_HRESULT(1, FACILITY_ITF, 0x020B);

inline void InitConsole ( ) { _setmode ( _fileno(stdout), _O_U16TEXT ); }

inline void Log ( LPCWSTR role, LPCWSTR fmt, ... )
{
    SYSTEMTIME st; ::GetLocalTime ( &st );
    wprintf ( L"[%02d:%02d:%02d.%03d tid=%lu %s] ",
              st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
              ::GetCurrentThreadId(), role );
    va_list ap; va_start ( ap, fmt );
    vwprintf ( fmt, ap );
    va_end ( ap );
    wprintf ( L"\n" );
    fflush ( stdout );
}

inline void LogMessage ( LPCWSTR role, LPCWSTR kind, LPCWSTR source, LPCWSTR text )
{
    wprintf ( L"\n[%s] %s from '%s':\n  > %s\n\n", role, kind, source, text );
    fflush ( stdout );
}

inline LPCWSTR HrName ( HRESULT hr )
{
    if ( hr == S_OK )              return L"S_OK";
    if ( hr == S_FALSE )           return L"S_FALSE";
    if ( hr == E_INVALIDARG )      return L"E_INVALIDARG";
    if ( hr == DISP_E_TYPEMISMATCH ) return L"DISP_E_TYPEMISMATCH";
    if ( hr == S_UNRELATED_LINK )  return L"P2PF_S_UNRELATED_LINK";
    if ( hr == E_CON_FACTORY )     return L"P2PF_E_CON_FACTORY";
    if ( hr == E_CON_DUPLICATE )   return L"P2PF_E_CON_DUPLICATE";
    if ( hr == E_RESERVED_TOPIC )  return L"P2PF_E_RESERVED_TOPIC";
    if ( hr == E_CLOSED )          return L"P2PF_E_CLOSED";
    if ( hr == E_HUB_DUPLICATE )   return L"P2PF_E_HUB_DUPLICATE";
    if ( hr == E_ENDPOINT )        return L"P2PF_E_ENDPOINT";
    if ( hr == E_UNRESOLVED )      return L"P2PF_E_UNRESOLVED";
    if ( hr == E_NO_HUB )          return L"P2PF_E_NO_HUB";
    if ( hr == E_LINK_PARTIAL )    return L"P2PF_E_LINK_PARTIAL";
    return L"(other)";
}

// ---------------------------------------------------------------------------
// Endpoint composers -- identical to FacadeExamples's light:: ones, and
// deliberately so: the endpoint string is the SAME string on both sides of the
// COM boundary. TargetCom does not parse it, reformat it or validate it; it
// SysAllocStrings it and hands it to p2pf::IP2PHub, which is the one place the
// grammar lives.
//
// That is the whole reason the eight typed Listen*/Connect* methods could go
// away without automation losing anything: `serial://COM5` is a BSTR, and a
// BSTR is the one type every automation caller already has. VBScript composes
// these with `&`; the range check that used to live in the COM layer's Port()
// helper is now P2PF_E_ENDPOINT out of the parser.
// ---------------------------------------------------------------------------
inline std::wstring TcpListen ( unsigned short port )
{
    // A listen must NOT name a host: the kernel binds INADDR_ANY regardless.
    return L"tcp://:" + std::to_wstring ( (unsigned)port );
}
inline std::wstring TcpDial ( LPCWSTR host, unsigned short port )
{
    return std::wstring ( L"tcp://" ) + host + L":" + std::to_wstring ( (unsigned)port );
}
inline std::wstring Pipe   ( LPCWSTR pipeName )    { return std::wstring ( L"pipe://" ) + pipeName; }
inline std::wstring Dmx    ( LPCWSTR serviceName ) { return std::wstring ( L"dmx://" ) + serviceName; }
inline std::wstring Serial ( unsigned short comPort )
{
    return L"serial://COM" + std::to_wstring ( (unsigned)comPort );
}

// ---------------------------------------------------------------------------
// Bstr -- the smallest possible RAII for the one COM type these harnesses
// cannot avoid. Methods take BSTR, not const wchar_t*.
// ---------------------------------------------------------------------------
class Bstr
{
  public:
    explicit Bstr ( LPCWSTR s = NULL ) { m_bs = ::SysAllocString ( s ? s : L"" ); }
    Bstr ( const Bstr& o )             { m_bs = ::SysAllocString ( o.m_bs ? o.m_bs : L"" ); }
   ~Bstr ( )                           { ::SysFreeString ( m_bs ); }
    Bstr& operator = ( const Bstr& o )
    {
        if ( this != &o ) { ::SysFreeString ( m_bs ); m_bs = ::SysAllocString ( o.m_bs ? o.m_bs : L"" ); }
        return *this;
    }
    operator BSTR ( ) const { return m_bs; }
  private:
    BSTR m_bs;
};

// ---------------------------------------------------------------------------
// Apartment -- CoInitializeEx/CoUninitialize, and the message pump an STA owes
// the runtime.
// ---------------------------------------------------------------------------
class Apartment
{
  public:
    Apartment ( )  { m_hr = ::CoInitializeEx ( NULL, COINIT_APARTMENTTHREADED ); }
   ~Apartment ( )  { if ( SUCCEEDED(m_hr) ) ::CoUninitialize(); }
    bool ok ( ) const { return SUCCEEDED(m_hr); }
  private:
    HRESULT m_hr;
};

inline void Pump ( )
{
    MSG msg;
    while ( ::PeekMessage ( &msg, NULL, 0, 0, PM_REMOVE ) )
    {
        ::TranslateMessage ( &msg );
        ::DispatchMessage ( &msg );
    }
}

// A "did it happen yet" flag whose wait() PUMPS -- without that, a marshalled
// event can never be delivered to this apartment and every harness would hang.
class Gate
{
  public:
    Gate ( ) : m_open ( 0 ) { }
    void open   ( )        { ::InterlockedExchange ( &m_open, 1 ); }
    bool isOpen ( ) const  { return ::InterlockedCompareExchange ( (volatile LONG*)&m_open, 0, 0 ) != 0; }
    bool wait   ( DWORD ms )
    {
        DWORD start = ::GetTickCount();
        for ( ;; )
        {
            Pump();
            if ( isOpen() ) return true;
            if ( ::GetTickCount() - start > ms ) return false;
            ::Sleep ( 5 );
        }
    }
  private:
    volatile LONG m_open;
};

// Wait for several gates at once, still pumping.
inline bool WaitAll ( Gate **gates, size_t count, DWORD ms )
{
    DWORD start = ::GetTickCount();
    for ( ;; )
    {
        Pump();
        bool all = true;
        for ( size_t i = 0; i < count; ++i ) if ( !gates[i]->isOpen() ) { all = false; break; }
        if ( all ) return true;
        if ( ::GetTickCount() - start > ms ) return false;
        ::Sleep ( 5 );
    }
}

// Pump for a fixed span (used where a harness must observe that something did
// NOT arrive).
inline void PumpFor ( DWORD ms )
{
    DWORD start = ::GetTickCount();
    while ( ::GetTickCount() - start < ms ) { Pump(); ::Sleep ( 5 ); }
}

// ---------------------------------------------------------------------------
// What a topic handler receives. Mirrors p2pf::Message, except the payload
// arrived as a SAFEARRAY and has already been unpacked.
// ---------------------------------------------------------------------------
struct Message
{
    const wchar_t *source;
    const wchar_t *topic;
    const BYTE    *payload;
    unsigned int   size;
    bool           broadcast;

    const wchar_t* text ( ) const
    {
        return ( payload && size >= sizeof(wchar_t) ) ? (const wchar_t*)payload : L"<no data>";
    }
};

typedef std::function<void(const Message&)>      MessageHandler;
typedef std::function<void(const wchar_t *peer)> PeerHandler;
typedef std::function<void(const wchar_t *what)> ErrorHandler;

// ---------------------------------------------------------------------------
// EventSink -- a hand-written IDispatch implementing the _IP2PHubEvents
// dispinterface, dispatching into std::function registries.
//
// This is the code an early-bound client has to write and a scripting client
// gets for free from its host. It is also exactly why a PowerShell client
// cannot sink these events: .NET needs an interop assembly for the coclass to
// build the equivalent of this class.
// ---------------------------------------------------------------------------
class EventSink : public IDispatch
{
  public:
    EventSink ( ) : m_lRef ( 1 ) { }

    STDMETHOD(QueryInterface) ( REFIID riid, void **ppv )
    {
        if ( !ppv ) return E_POINTER;
        if ( ::IsEqualIID ( riid, IID_IUnknown ) ||
             ::IsEqualIID ( riid, IID_IDispatch ) ||
             ::IsEqualIID ( riid, DIID__IP2PHubEvents ) )
        {
            *ppv = static_cast<IDispatch*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)  ( ) { return ::InterlockedIncrement ( &m_lRef ); }
    STDMETHOD_(ULONG, Release) ( )
    {
        LONG l = ::InterlockedDecrement ( &m_lRef );
        if ( l == 0 ) delete this;
        return l;
    }

    STDMETHOD(GetTypeInfoCount) ( UINT *p ) { if (p) *p = 0; return S_OK; }
    STDMETHOD(GetTypeInfo)      ( UINT, LCID, ITypeInfo** ) { return E_NOTIMPL; }
    STDMETHOD(GetIDsOfNames)    ( REFIID, LPOLESTR*, UINT, LCID, DISPID* ) { return E_NOTIMPL; }

    STDMETHOD(Invoke) ( DISPID dispid, REFIID, LCID, WORD, DISPPARAMS *pdp,
                        VARIANT*, EXCEPINFO*, UINT* )
    {
        if ( !pdp ) return E_POINTER;

        switch ( dispid )
        {
          case 1:   // OnMessage(source, topic, payload, broadcast) -- reversed
          {
            if ( pdp->cArgs != 4 ) return DISP_E_BADPARAMCOUNT;

            std::vector<BYTE> bytes;
            Unpack ( pdp->rgvarg[1], bytes );

            Message m;
            m.source    = pdp->rgvarg[3].bstrVal ? pdp->rgvarg[3].bstrVal : L"";
            m.topic     = pdp->rgvarg[2].bstrVal ? pdp->rgvarg[2].bstrVal : L"";
            m.payload   = bytes.empty() ? NULL : &bytes[0];
            m.size      = (unsigned int)bytes.size();
            m.broadcast = ( pdp->rgvarg[0].boolVal != VARIANT_FALSE );

            std::map<std::wstring, MessageHandler>::iterator it = topics.find ( m.topic );
            if ( it != topics.end() && it->second ) it->second ( m );
            else if ( fallback )                    fallback ( m );
            break;
          }
          case 2: if ( peerUp )   peerUp   ( Arg0 ( pdp ) ); break;
          case 3: if ( peerDown ) peerDown ( Arg0 ( pdp ) ); break;
          case 4: if ( error )    error    ( Arg0 ( pdp ) ); break;
          default: return DISP_E_MEMBERNOTFOUND;
        }
        return S_OK;
    }

    std::map<std::wstring, MessageHandler> topics;
    MessageHandler fallback;
    PeerHandler    peerUp, peerDown;
    ErrorHandler   error;

  private:
    static LPCWSTR Arg0 ( DISPPARAMS *pdp )
    {
        return ( pdp->cArgs >= 1 && pdp->rgvarg[0].bstrVal ) ? pdp->rgvarg[0].bstrVal : L"";
    }

    static void Unpack ( const VARIANT& v, std::vector<BYTE>& out )
    {
        out.clear();
        if ( ( v.vt & VT_ARRAY ) == 0 || v.parray == NULL ) return;

        LONG lo = 0, hi = -1;
        ::SafeArrayGetLBound ( v.parray, 1, &lo );
        ::SafeArrayGetUBound ( v.parray, 1, &hi );
        if ( hi < lo ) return;

        void *p = NULL;
        if ( SUCCEEDED ( ::SafeArrayAccessData ( v.parray, &p ) ) )
        {
            const BYTE *b = (const BYTE*)p;
            out.assign ( b, b + ( hi - lo + 1 ) );
            ::SafeArrayUnaccessData ( v.parray );
        }
    }

    LONG m_lRef;
};

// Wrap raw bytes as the VARIANT(SAFEARRAY of VT_UI1) that Send/Broadcast take.
inline HRESULT MakeBytes ( const void *data, unsigned int cb, VARIANT *pv )
{
    ::VariantInit ( pv );
    SAFEARRAY *psa = ::SafeArrayCreateVector ( VT_UI1, 0, cb );
    if ( !psa ) return E_OUTOFMEMORY;
    if ( cb )
    {
        void *dst = NULL;
        HRESULT hr = ::SafeArrayAccessData ( psa, &dst );
        if ( FAILED(hr) ) { ::SafeArrayDestroy ( psa ); return hr; }
        ::memcpy ( dst, data, cb );
        ::SafeArrayUnaccessData ( psa );
    }
    pv->vt = VT_ARRAY | VT_UI1;
    pv->parray = psa;
    return S_OK;
}

class Network;

// ---------------------------------------------------------------------------
// Hub -- IP2PHubCom plus its connection point, with facade-shaped verbs.
// ---------------------------------------------------------------------------
class Hub
{
  public:
    Hub ( ) : m_pHub ( NULL ), m_pCP ( NULL ), m_pSink ( NULL ), m_dwCookie ( 0 ) { }
   ~Hub ( ) { reset(); }

    Hub             ( const Hub& ) = delete;
    Hub& operator = ( const Hub& ) = delete;

    // Adopts a hub returned by Network::createHub and advises a fresh sink.
    HRESULT attach ( IP2PHubCom *pHub )
    {
        reset();
        if ( !pHub ) return E_POINTER;
        m_pHub = pHub;                       // takes the caller's reference

        IConnectionPointContainer *pCPC = NULL;
        HRESULT hr = m_pHub->QueryInterface ( IID_IConnectionPointContainer, (void**)&pCPC );
        if ( FAILED(hr) ) return hr;

        hr = pCPC->FindConnectionPoint ( DIID__IP2PHubEvents, &m_pCP );
        pCPC->Release();
        if ( FAILED(hr) ) return hr;

        m_pSink = new EventSink();
        hr = m_pCP->Advise ( m_pSink, &m_dwCookie );
        return hr;
    }

    bool ok ( ) const { return m_pHub != NULL && m_dwCookie != 0; }

    // --- handler registration (the same registry the facade's Fn layer has) --
    Hub& onTopic   ( const std::wstring& t, MessageHandler h ) { m_pSink->topics[t] = h; return *this; }
    Hub& onMessage ( MessageHandler h ) { m_pSink->fallback = h; return *this; }
    Hub& onPeerUp  ( PeerHandler h )    { m_pSink->peerUp   = h; return *this; }
    Hub& onPeerDown( PeerHandler h )    { m_pSink->peerDown = h; return *this; }
    Hub& onError   ( ErrorHandler h )   { m_pSink->error    = h; return *this; }

    // --- transports ---------------------------------------------------------
    // Two verbs, four transports. Compose the endpoint with com::TcpListen,
    // TcpDial, Pipe, Dmx or Serial above -- or pass any string you like, since
    // it is a configuration value now and not a choice of method.
    HRESULT listen  ( LPCWSTR toPeer, LPCWSTR endpoint )
                              { return m_pHub->Listen  ( Bstr(toPeer), Bstr(endpoint) ); }
    HRESULT connect ( LPCWSTR toPeer, LPCWSTR endpoint )
                              { return m_pHub->Connect ( Bstr(toPeer), Bstr(endpoint) ); }

    // --- sending -------------------------------------------------------------
    HRESULT sendText ( LPCWSTR dest, LPCWSTR topic, LPCWSTR text )
    {
        return m_pHub->SendText ( Bstr(dest), Bstr(topic), Bstr(text) );
    }

    HRESULT send ( LPCWSTR dest, LPCWSTR topic, const void *data, unsigned int cb )
    {
        VARIANT v;
        HRESULT hr = MakeBytes ( data, cb, &v );
        if ( FAILED(hr) ) return hr;
        hr = m_pHub->Send ( Bstr(dest), Bstr(topic), v );
        ::VariantClear ( &v );
        return hr;
    }

    // Returns S_OK with *pDelivered telling you whether anyone was up -- the
    // automation-shaped replacement for the facade's S_FALSE.
    HRESULT broadcast ( LPCWSTR topic, const void *data, unsigned int cb, bool *pDelivered = NULL )
    {
        VARIANT v;
        HRESULT hr = MakeBytes ( data, cb, &v );
        if ( FAILED(hr) ) return hr;
        VARIANT_BOOL vb = VARIANT_FALSE;
        hr = m_pHub->Broadcast ( Bstr(topic), v, &vb );
        ::VariantClear ( &v );
        if ( pDelivered ) *pDelivered = ( vb != VARIANT_FALSE );
        return hr;
    }

    // --- properties ----------------------------------------------------------
    std::wstring address ( ) const
    {
        BSTR bs = NULL;
        if ( FAILED ( m_pHub->get_Address ( &bs ) ) || !bs ) return std::wstring();
        std::wstring s ( bs );
        ::SysFreeString ( bs );
        return s;
    }

    bool isPeerUp ( LPCWSTR peer ) const
    {
        VARIANT_BOOL vb = VARIANT_FALSE;
        return SUCCEEDED ( m_pHub->IsPeerUp ( Bstr(peer), &vb ) ) && vb != VARIANT_FALSE;
    }

    HRESULT close ( ) { return m_pHub ? m_pHub->Close() : S_OK; }

    IP2PHubCom* raw ( ) const { return m_pHub; }

  private:
    void reset ( )
    {
        if ( m_pCP && m_dwCookie ) { m_pCP->Unadvise ( m_dwCookie ); m_dwCookie = 0; }
        if ( m_pSink ) { m_pSink->Release(); m_pSink = NULL; }
        if ( m_pCP )   { m_pCP->Release();   m_pCP = NULL; }
        if ( m_pHub )  { m_pHub->Close(); m_pHub->Release(); m_pHub = NULL; }
    }

    IP2PHubCom       *m_pHub;
    IConnectionPoint *m_pCP;
    EventSink        *m_pSink;
    DWORD             m_dwCookie;
};

// ---------------------------------------------------------------------------
// Network -- CoCreateInstance(TargetCom.P2PNetwork) and the hub factory.
// ---------------------------------------------------------------------------
class Network
{
  public:
    Network ( ) : m_pNet ( NULL )
    {
        m_hr = ::CoCreateInstance ( CLSID_P2PNetwork, NULL, CLSCTX_INPROC_SERVER,
                                    IID_IP2PNetworkCom, (void**)&m_pNet );
    }
   ~Network ( ) { if ( m_pNet ) m_pNet->Release(); }

    Network             ( const Network& ) = delete;
    Network& operator = ( const Network& ) = delete;

    bool    ok ( ) const { return SUCCEEDED(m_hr) && m_pNet != NULL; }
    HRESULT hr ( ) const { return m_hr; }

    HRESULT createHub ( LPCWSTR address, Hub& hub )
    {
        IP2PHubCom *p = NULL;
        HRESULT h = m_pNet->CreateHub ( Bstr(address), &p );
        if ( FAILED(h) ) return h;
        return hub.attach ( p );
    }

    std::wstring versionString ( ) const
    {
        BSTR bs = NULL;
        if ( FAILED ( m_pNet->get_VersionString ( &bs ) ) || !bs ) return std::wstring();
        std::wstring s ( bs );
        ::SysFreeString ( bs );
        return s;
    }

    LONG maxPayload ( ) const
    {
        LONG v = 0;
        m_pNet->get_MaxPayload ( &v );
        return v;
    }

  private:
    IP2PNetworkCom *m_pNet;
    HRESULT         m_hr;
};

// Common failure exit: the DLL is not registered, or a dependency is missing.
inline int SetupFailure ( LPCWSTR what, HRESULT hr )
{
    Log ( L"MAIN", L"SETUP: %s failed (0x%08lX %s)", what, (unsigned long)hr, HrName ( hr ) );
    if ( hr == REGDB_E_CLASSNOTREG )
        wprintf ( L"\nTargetCom is not registered. Run:\n"
                  L"    run_all.ps1            (registers per-user, runs, unregisters)\n"
                  L"or  regsvr32 /n /i:user \"...\\TargetFacade\\com\\x64\\Debug\\TargetCom.dll\"\n" );
    fflush ( stdout );
    return EXIT_SETUP;
}

inline int Verdict ( bool ok, LPCWSTR okMsg, LPCWSTR failMsg )
{
    Log ( L"MAIN", ok ? L"SUCCESS - %s" : L"TIMEOUT - %s", ok ? okMsg : failMsg );
    int code = ok ? EXIT_SUCCESS_ : EXIT_TIMEOUT;
    wprintf ( L"Done (exit=%d).\n", code );
    fflush ( stdout );
    return code;
}

} // namespace com

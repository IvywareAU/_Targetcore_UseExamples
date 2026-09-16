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
// LightHarness.h
//
// The small amount of scaffolding the Light harnesses still need, once the
// facade has absorbed everything else.
//
// What ISN'T here is the point of this directory. The originals in
// DirectExamples each carried, per file:
//
//   * a stdafx.h pulling in afx.h/afxwin.h/afxext.h/afxmt.h/afxtempl.h,
//     WinSock2.h, mswsock.h and the Targetcore headers;
//   * `CWinApp theApp;` -- MFC's one-instance-per-exe rule;
//   * a _CrtSetReportHook assert trap, because a debug ASSERT inside the
//     kernel would otherwise pop a MODAL DIALOG and hang a headless run;
//   * StartupP2Pmsg / WSAStartup / SpawnHub / CloseHub / CleanupP2Pmsg /
//     WSACleanup, in that exact order, on every exit path;
//   * a P2PeerHub subclass with seven On_Con* trace overrides just to see how
//     far a handshake got.
//
// None of that survives the facade. `p2pf::Network` is the startup/shutdown,
// `p2pf::Hub` is the pump, `onTopic`/`onPeerUp` are the map, and the facade
// never shows a modal box (that was a hard-won fix -- see the facade README on
// HasDroppedOut). So all that is left is: print a line, and wait for a thing.
//
// Exit-code contract, kept identical to the originals so the two trees are
// directly comparable:
//   0 = SUCCESS
//   1 = SETUP    (startup/arming failure)
//   3 = TIMEOUT  (no assert, but the expected traffic never arrived)
//   2 = was "an MFC/CRT assertion fired". It is now unreachable by
//       construction: there is no MFC in these processes, and the facade
//       reports failures as HRESULTs and OnError callbacks instead.

#pragma once

#include <windows.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <string>

namespace light {

const int EXIT_SUCCESS_   = 0;
const int EXIT_SETUP      = 1;
const int EXIT_TIMEOUT    = 3;

// Put stdout in UTF-16 mode so wprintf emits Unicode correctly, exactly as the
// originals did.
inline void InitConsole ( )
{
    _setmode ( _fileno(stdout), _O_U16TEXT );
}

// Timestamped, flushed milestone log -- same shape as the originals' LogAt().
inline void Log ( LPCWSTR role, LPCWSTR fmt, ... )
{
    SYSTEMTIME st; ::GetLocalTime ( &st );
    wprintf ( L"[%02d:%02d:%02d.%03d tid=%lu %s] ",
              st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
              ::GetCurrentThreadId(), role );

    va_list ap;
    va_start ( ap, fmt );
    vwprintf ( fmt, ap );
    va_end ( ap );

    wprintf ( L"\n" );
    fflush ( stdout );
}

// Print an arriving message the way the originals printed one.
inline void LogMessage ( LPCWSTR role, LPCWSTR kind, LPCWSTR source, LPCWSTR text )
{
    wprintf ( L"\n[%s] %s from '%s':\n  > %s\n\n", role, kind, source, text );
    fflush ( stdout );
}

// ---------------------------------------------------------------------------
// Gate -- a manual "did the thing happen yet" event. Replaces the raw
// CreateEvent/WaitForSingleObject/CloseHandle triplet each original repeated.
// ---------------------------------------------------------------------------
class Gate
{
  public:
    Gate ( )  { m_h = ::CreateEvent ( NULL, TRUE, FALSE, NULL ); }   // manual reset
   ~Gate ( )  { if ( m_h ) ::CloseHandle ( m_h ); }

    Gate             ( const Gate& ) = delete;
    Gate& operator = ( const Gate& ) = delete;

    void open  ( )              { if ( m_h ) ::SetEvent ( m_h ); }
    bool wait  ( DWORD ms )     { return m_h && ::WaitForSingleObject ( m_h, ms ) == WAIT_OBJECT_0; }
    bool isOpen( ) const        { return m_h && ::WaitForSingleObject ( m_h, 0 ) == WAIT_OBJECT_0; }
    HANDLE raw ( ) const        { return m_h; }

  private:
    HANDLE m_h;
};

// Wait for several gates at once (the "both hubs got it" case).
inline bool WaitAll ( Gate **gates, DWORD count, DWORD ms )
{
    HANDLE h[MAXIMUM_WAIT_OBJECTS];
    if ( count > MAXIMUM_WAIT_OBJECTS ) return false;
    for ( DWORD i = 0; i < count; ++i ) h[i] = gates[i]->raw();
    return ::WaitForMultipleObjects ( count, h, TRUE, ms ) == WAIT_OBJECT_0;
}

// A payload sent with sendText() is the UTF-16 string including its
// terminator, so a received p2pf::Message can be read back as text directly.
inline LPCWSTR TextOf ( const void *payload, unsigned int size )
{
    return ( payload != NULL && size >= sizeof(wchar_t) ) ? (LPCWSTR)payload
                                                          : L"<no data>";
}

// ---------------------------------------------------------------------------
// Endpoint composers.
//
// The facade has ONE arming pair -- listen(toPeer, endpoint) /
// connect(toPeer, endpoint) -- and the transport now lives in the endpoint
// STRING rather than in the method name. That is the point: an endpoint is a
// configuration value, so a harness could read it from argv without changing
// shape. These harnesses keep their typed constants (a port is still a port in
// the source), so the composing happens here, once, instead of eleven times.
//
// The parser takes a pipe name and a Dmx service name VERBATIM after the
// scheme, so a full \\.\pipe\name path passes through untouched.
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
inline std::wstring Pipe ( LPCWSTR pipeName )
{
    return std::wstring ( L"pipe://" ) + pipeName;
}
inline std::wstring Dmx ( LPCWSTR serviceName )
{
    return std::wstring ( L"dmx://" ) + serviceName;
}
inline std::wstring Serial ( unsigned short comPort )
{
    return L"serial://COM" + std::to_wstring ( (unsigned)comPort );
}

// Report a facade HRESULT with its facade-specific name, since these harnesses
// exist to show what the error contract looks like.
inline LPCWSTR HrName ( HRESULT hr )
{
    if ( hr == S_OK )                        return L"S_OK";
    if ( hr == S_FALSE )                     return L"S_FALSE";
    // A SUCCESS code, not an error: the link IS armed, but the two addresses
    // are neither ancestor nor descendant, so it can never be a transit hop.
    // Every Server/Client pair in this tree is a sibling pair -- see the
    // README -- so this is the expected answer from most of the arming calls
    // below, and FAILED() correctly does not see it.
    if ( hr == p2pf::P2PF_S_UNRELATED_LINK ) return L"P2PF_S_UNRELATED_LINK";
    if ( hr == p2pf::P2PF_E_ABI_MISMATCH )   return L"P2PF_E_ABI_MISMATCH";
    if ( hr == p2pf::P2PF_E_STARTUP )        return L"P2PF_E_STARTUP";
    if ( hr == p2pf::P2PF_E_HUB_SPAWN )      return L"P2PF_E_HUB_SPAWN";
    if ( hr == p2pf::P2PF_E_CON_FACTORY )    return L"P2PF_E_CON_FACTORY";
    if ( hr == p2pf::P2PF_E_CON_DUPLICATE )  return L"P2PF_E_CON_DUPLICATE";
    if ( hr == p2pf::P2PF_E_RESERVED_TOPIC ) return L"P2PF_E_RESERVED_TOPIC";
    if ( hr == p2pf::P2PF_E_CLOSED )         return L"P2PF_E_CLOSED";
    if ( hr == p2pf::P2PF_E_HUB_DUPLICATE )  return L"P2PF_E_HUB_DUPLICATE";
    if ( hr == p2pf::P2PF_E_ENDPOINT )       return L"P2PF_E_ENDPOINT";
    if ( hr == p2pf::P2PF_E_UNRESOLVED )     return L"P2PF_E_UNRESOLVED";
    if ( hr == p2pf::P2PF_E_NO_HUB )         return L"P2PF_E_NO_HUB";
    if ( hr == p2pf::P2PF_E_LINK_PARTIAL )   return L"P2PF_E_LINK_PARTIAL";
    return L"(other)";
}

// Announce the verdict in the originals' words.
inline int Verdict ( bool ok, LPCWSTR okMsg, LPCWSTR failMsg )
{
    Log ( L"MAIN", ok ? L"SUCCESS - %s" : L"TIMEOUT - %s", ok ? okMsg : failMsg );
    int code = ok ? EXIT_SUCCESS_ : EXIT_TIMEOUT;
    wprintf ( L"Done (exit=%d).\n", code );
    fflush ( stdout );
    return code;
}

} // namespace light

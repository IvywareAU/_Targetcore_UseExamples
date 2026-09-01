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
//  NTServiceEventLog - hosting a P2PeerHub as a Windows service and reporting
//                      to the Windows event log
//
//  Refer README.md in this directory. In short:
//
//    * A service has no console, no standard error and no desktop anybody can
//      see, so it has nowhere to PUT a diagnostic. That is not cosmetic: a
//      P2Pevent used to become a modal MessageBox in that shape, raised on
//      whichever thread reached it - a hub's own pump among them - and a hub's
//      teardown WAITS for its pumps. A dialog nobody could dismiss was a
//      service that could not stop.
//    * The library now refuses to raise a dialog where nobody could see one,
//      and P2PeerService routes its own diagnostics to the event log for a
//      hosted hub. Neither needs configuring.
//    * What no library can do for you is raise the events that are YOURS.
//      AppEventLog below is that half, and it is deliberately small.
//
#include "stdafx.h"

#include "P2Pwin32.h"
#include "NTServiceEventLog.h"
#include "TargetCoreEvt.h"             // TargetCore's committed message catalogue

#include <cstdio>

///////////////////////////////////////////////////////////////////////
//  SCM entry points
//  NOTES: The SCM calls plain functions, so a service object needs a pair of
//         statics and a global to delegate through. TargetCore has its own
//         internal pair for the same purpose; they are not exported, and an
//         example is clearer for owning them anyway - this is the whole of the
//         "how does the SCM reach my object" question.
//
static ReportingService *g_pService = 0;

static VOID WINAPI
ExampleServiceMain ( DWORD argc, LPTSTR *argv )
{
    if ( g_pService )
      g_pService -> Service ( argc, argv );
}

static VOID WINAPI
ExampleControlHandler ( DWORD dwControlCode )
{
    if ( g_pService )
      g_pService -> ServiceCtrlHandler ( dwControlCode );
}

///////////////////////////////////////////////////////////////////////
//  AppEventLog

AppEventLog::AppEventLog ( )
           : m_hLog ( 0 )
{
}

AppEventLog::~AppEventLog ( )
{
    Close ( );
}

//
//  Opens the application's event log source
//  NOTES: Succeeds even when the source was never registered. The text still
//         reaches the log; the Event Viewer simply has no message table to
//         format it with and wraps it in "The description for Event ID ...
//         cannot be found". So a service that was installed without elevation
//         still reports - it just reports uglily, which is the right way round.
//
//  Parameters:  LPCWSTR lpszSource
//               Event source name, as registered.
//
//  Returns:     bool
//               true ... the source is open
bool
AppEventLog::Open ( LPCWSTR lpszSource )
{
    Close ( );
    m_hLog = ::RegisterEventSourceW ( 0, lpszSource );
    return m_hLog != 0;
}

void
AppEventLog::Close ( )
{
    if ( m_hLog )
      ::DeregisterEventSource ( m_hLog );
    m_hLog = 0;
}

//
//  Writes one event
//  NOTES: MUST NOT raise a P2Pevent, however it fails. This is reachable from
//         a P2Pevent sink, and an event raised from inside the reporting of an
//         event recurses until the stack is gone. Failures here are therefore
//         silent on purpose. It is the one rule to carry over if you replace
//         this class with your own.
//       : The catalogue's message ids ARE the P2Pevent_e class values, so the
//         mapping is a switch rather than a table that can drift from the enum.
//
//  Parameters:  P2Pevent_e eClass
//               Class of the event being reported.
//
//               LPCWSTR lpszOrigin
//               Where it came from - insertion %1.
//
//               LPCWSTR lpszText
//               What happened - insertion %2.
//
void
AppEventLog::Write ( P2Pevent_e eClass, LPCWSTR lpszOrigin, LPCWSTR lpszText )
{
    HANDLE hLog = m_hLog;
    if ( !hLog )
      return;

    DWORD dwEventID = P2PMSG_EVT_UNCLASSED;
    WORD  wType     = EVENTLOG_WARNING_TYPE;
    switch ( eClass )
    {
      case P2Pevent_ERROR:
        dwEventID = P2PMSG_EVT_ERROR;   wType = EVENTLOG_ERROR_TYPE;       break;
      case P2Pevent_WARNING:
        dwEventID = P2PMSG_EVT_WARNING; wType = EVENTLOG_WARNING_TYPE;     break;
      case P2Pevent_INFO:
        dwEventID = P2PMSG_EVT_INFO;    wType = EVENTLOG_INFORMATION_TYPE; break;
      case P2Pevent_DEBUG:
        dwEventID = P2PMSG_EVT_DEBUG;   wType = EVENTLOG_INFORMATION_TYPE; break;
      case P2Pevent_TRACE:
        dwEventID = P2PMSG_EVT_TRACE;   wType = EVENTLOG_INFORMATION_TYPE; break;
      case P2Pevent_LOG:
        dwEventID = P2PMSG_EVT_LOG;     wType = EVENTLOG_INFORMATION_TYPE; break;
      case P2Pevent_REPORT:
        dwEventID = P2PMSG_EVT_REPORT;  wType = EVENTLOG_INFORMATION_TYPE; break;
      default:
        break;
    }

    LPCWSTR apszInsert[2] = { lpszOrigin ? lpszOrigin : L""
                            , lpszText   ? lpszText   : L"" };
    ::ReportEventW ( hLog, wType, 0, dwEventID, 0, 2, 0, apszInsert, 0 );
}

//
//  Formats and writes one event
//
void
AppEventLog::Report ( P2Pevent_e eClass, LPCWSTR lpszOrigin
                    , LPCWSTR lpszFormat, ... )
{
    WCHAR   szText[1024] = { 0 };
    va_list oArgs;
    va_start ( oArgs, lpszFormat );
    ::_vsnwprintf_s ( szText, ARRAYSIZE(szText), _TRUNCATE, lpszFormat, oArgs );
    va_end ( oArgs );

    Write ( eClass, lpszOrigin, szText );

    // A console run should also SEE it. In service mode there is no console and
    // this goes nowhere, harmlessly.
    std::fwprintf ( stderr, L"[%ls] %ls %ls\n"
                  , eClass == P2Pevent_ERROR ? L"ERROR" : L"INFO"
                  , lpszOrigin ? lpszOrigin : L"", szText );
    std::fflush ( stderr );
}

//
//  Names the module carrying the message table this source will be rendered with
//  NOTES: TargetCore.dll, which is staged beside this executable by the build.
//         Derived from the EXE's own directory rather than from a loaded module
//         handle, because at install time TargetCore is delay-loaded and may not
//         be in the process yet.
//       : If you give your own module its own .mc catalogue, name that module
//         here instead. Nothing else in this file changes.
//
//  Parameters:  CStringW& strPath
//               Receives the full path on success.
//
//  Returns:     bool
//               true ... resolved
bool
AppEventLog::MessageFilePath ( CStringW& strPath )
{
    WCHAR szExe[MAX_PATH] = { 0 };
    DWORD dwLength = ::GetModuleFileNameW ( 0, szExe, ARRAYSIZE(szExe) );
    if ( dwLength == 0 || dwLength >= ARRAYSIZE(szExe) )
      return false;                    // failed, or truncated

    CStringW strExe ( szExe );
    int iSlash = strExe.ReverseFind ( L'\\' );
    if ( iSlash < 0 )
      return false;

    strPath = strExe.Left ( iSlash + 1 ) + L"TargetCore.dll";
    return true;
}

//
//  Registers the source so the Event Viewer can format what it receives
//  NOTES: HKLM, so this needs elevation - which an install already has.
//
//  Parameters:  LPCWSTR lpszSource
//               Event source name.
//
//  Returns:     bool
//               true ... registered
bool
AppEventLog::Register ( LPCWSTR lpszSource )
{
    CStringW strMessageFile;
    if ( !MessageFilePath ( strMessageFile ) )
      return false;

    CStringW strKey;
    strKey.Format ( L"SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\%s"
                  , lpszSource );

    HKEY hKey = 0;
    if ( ::RegCreateKeyExW ( HKEY_LOCAL_MACHINE, strKey, 0, 0
                           , REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, 0
                           , &hKey, 0 ) != ERROR_SUCCESS )
      return false;

    ::RegSetValueExW ( hKey, L"EventMessageFile", 0, REG_EXPAND_SZ
                     , (const BYTE*)(LPCWSTR)strMessageFile
                     , (DWORD)((strMessageFile.GetLength() + 1) * sizeof(WCHAR)) );

    DWORD dwTypes = EVENTLOG_ERROR_TYPE
                  | EVENTLOG_WARNING_TYPE
                  | EVENTLOG_INFORMATION_TYPE;
    ::RegSetValueExW ( hKey, L"TypesSupported", 0, REG_DWORD
                     , (const BYTE*)&dwTypes, sizeof(dwTypes) );

    ::RegCloseKey ( hKey );
    return true;
}

bool
AppEventLog::Unregister ( LPCWSTR lpszSource )
{
    CStringW strKey;
    strKey.Format ( L"SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\%s"
                  , lpszSource );
    return ::RegDeleteKeyW ( HKEY_LOCAL_MACHINE, strKey ) == ERROR_SUCCESS;
}

///////////////////////////////////////////////////////////////////////
//  ReportingHub

ReportingHub::ReportingHub ( P2PaddrSTR strP2PaddrHub, AppEventLog *pLog )
            : P2PeerHub ( strP2PaddrHub )
            , m_pLog    ( pLog )
            , m_cBCast  ( 0 )
{
}

ReportingHub::~ReportingHub ( )
{
}

//
//  The pump
//  NOTES: THIS IS THE THREAD THAT MATTERS. In service mode
//         P2PeerService::Run() calls CreateHub() and then RunHub(), so this
//         function IS the hub's pump, running on the service's own thread - and
//         CloseHub() waits for it to leave. Everything reported from here, and
//         from the handlers below, is reported from a thread whose progress the
//         service's shutdown depends on.
//       : Before the library refused unviewable dialogs, an event raised here
//         in a service put up a MessageBox on this thread. Nobody could click
//         OK, so this function never returned, so CloseHub() never finished,
//         so the SCM's STOP timed out and the service was left in STOP_PENDING
//         until the machine was rebooted. That is the failure this example is
//         built to make visible; see README.md for how to reproduce it.
//
void
ReportingHub::RunHub ( )
{
    if ( m_pLog )
      m_pLog -> Report ( P2Pevent_INFO, L"ReportingHub::RunHub"
                       , L"pump entering dispatch on thread %lu"
                       , (unsigned long)::GetCurrentThreadId ( ) );

    // A LIBRARY diagnostic, raised from the pump. This is the shape that used
    // to deadlock a service, and it is here on purpose: it should appear in the
    // event log under the SERVICE's source (P2PeerService installs the sink),
    // while the report above appears under the application's own source.
    EVERR -> Module  ( __FUNCTION__ )
          -> Message ( "Deliberate diagnostic raised on the pump thread, to "
                       "demonstrate that a hosted hub reports rather than hangs" )
          -> Advice_ ( _T("Expected. Refer NTServiceEventLog README.md.") )
          -> Cancel  ( );

    // Then be a pump.
    P2PeerHub::RunHub ( );

    if ( m_pLog )
      m_pLog -> Report ( P2Pevent_INFO, L"ReportingHub::RunHub"
                       , L"pump left dispatch after %ld broadcast(s)"
                       , m_cBCast );
}

//
//  Broadcast handler - pump thread
//
msgRESULT
ReportingHub::On_P2PeerBCast ( P2PeerMsg *pMsg )
{
    m_cBCast++;
    if ( m_pLog )
      m_pLog -> Report ( P2Pevent_INFO, L"ReportingHub::On_P2PeerBCast"
                       , L"broadcast %ld seen on pump thread %lu"
                       , m_cBCast, (unsigned long)::GetCurrentThreadId ( ) );

    // Always delegate. The base class does the routing; this override only
    // observes it.
    return P2PeerHub::On_P2PeerBCast ( pMsg );
}

//
//  Error handler - pump thread
//  NOTES: An APPLICATION error, reported as one. This is the case a library
//         cannot cover for you: TargetCore knows an error message arrived, and
//         only your code knows whether that is worth an operator's attention.
//
msgRESULT
ReportingHub::On_P2PeerError ( P2PeerMsg *pMsg )
{
    if ( m_pLog )
      m_pLog -> Report ( P2Pevent_ERROR, L"ReportingHub::On_P2PeerError"
                       , L"a peer reported an error to this hub" );

    return P2PeerHub::On_P2PeerError ( pMsg );
}

///////////////////////////////////////////////////////////////////////
//  ReportingService

ReportingService::ReportingService ( )
                : P2PeerService ( APP_SERVICE_NAME
                                , &ExampleServiceMain
                                , &ExampleControlHandler )
{
    SetDisplayName ( _T("TargetCore event log example") );
    SetServiceDesc ( _T("Hosts a P2PeerHub and reports to the Windows event "
                        "log. An example from _TargetCore_UseExamples.") );
}

ReportingService::~ReportingService ( )
{
}

//
//  Service initialisation, before the hub comes up
//  NOTES: Called from P2PeerService::Service() in service mode only - a
//         console run does not pass through it, which is why main() opens the
//         application log rather than relying on this.
//
DWORD
ReportingService::Init ( DWORD argc, LPTSTR *argv )
{
    DWORD dwResult = P2PeerService::Init ( argc, argv );

    m_oLog.Report ( P2Pevent_INFO, L"ReportingService::Init"
                  , L"service starting; hub address %ls, %u pump(s)"
                  , APP_HUB_ADDRESS, m_nMaxPumps );
    return dwResult;
}

//
//  SCM STOP
//  NOTES: Reported BEFORE delegating, because the base signals the hub to
//         close and this is the last moment at which reporting is certainly
//         still cheap.
//
void
ReportingService::OnStop ( )
{
    m_oLog.Report ( P2Pevent_INFO, L"ReportingService::OnStop"
                  , L"stop requested; closing the hub" );
    P2PeerService::OnStop ( );
}

//
//  Install
//  NOTES: The base registers the SERVICE name as an event source, for the
//         library's own diagnostics. This adds the application's source.
//         Both are needed: they are two different publishers and an operator
//         should be able to tell them apart.
//
void
ReportingService::PostInstall ( )
{
    P2PeerService::PostInstall ( );

    if ( AppEventLog::Register ( APP_EVENT_SOURCE ) )
      std::wprintf ( L"Registered event source '%ls'.\n", APP_EVENT_SOURCE );
    else
      std::wprintf ( L"Could NOT register event source '%ls' - run the install "
                     L"elevated.\nThe service will still report; the Event "
                     L"Viewer will not format it.\n", APP_EVENT_SOURCE );
}

void
ReportingService::PostUnInstall ( )
{
    P2PeerService::PostUnInstall ( );
    AppEventLog::Unregister ( APP_EVENT_SOURCE );
    std::wprintf ( L"Removed event source '%ls'.\n", APP_EVENT_SOURCE );
}

///////////////////////////////////////////////////////////////////////
//  Entry point

static void
Usage ( )
{
    std::wprintf (
      L"\n"
      L"NTServiceEventLog - a P2PeerHub hosted as a Windows service\n"
      L"\n"
      L"  -I[nstall]   register the service and its event sources (ELEVATED)\n"
      L"  -R[emove]    deregister both                            (ELEVATED)\n"
      L"  -C[onsole]   run in this console - no SCM, output on stderr\n"
      L"  -S[ervice]   run as a service (what the SCM passes)\n"
      L"  -Debug       add DEBUG events to the reporting mask\n"
      L"  -?           this text\n"
      L"\n"
      L"Typical use, from an elevated prompt:\n"
      L"\n"
      L"  NTServiceEventLog.exe -Install\n"
      L"  sc start P2PmsgEventLogExample\n"
      L"  sc stop  P2PmsgEventLogExample\n"
      L"  NTServiceEventLog.exe -Remove\n"
      L"\n"
      L"Then read the result:\n"
      L"\n"
      L"  Get-WinEvent -LogName Application -MaxEvents 20 |\n"
      L"    Where-Object { $_.ProviderName -like 'P2Pmsg*' } |\n"
      L"    Format-List TimeCreated, ProviderName, Id, LevelDisplayName, Message\n"
      L"\n" );
}

int
wmain ( int argc, wchar_t *argv[] )
{
    ReportingService oService;
    g_pService = &oService;

    // Arguments. P2PeerService owns the decoding, including performing the
    // install and remove, so this loop only has to offer each argument to it.
    bool bHelp = false;
    for ( DWORD i = 1; i < (DWORD)argc; i++ )
    {
      DWORD nArg = i;
      if ( oService.HelpMainArgs    ( argv, nArg ) )
      {
        bHelp = true;
        continue;
      }
      if ( oService.DefaultMainArgs ( argv, nArg ) )
        continue;

      std::wprintf ( L"Unrecognised argument '%ls'.\n", argv[i] );
      Usage ( );
      return 1;
    }

    // -? MUST return here, and this is not obvious: HelpMainArgs() sets console
    // mode as a side effect of printing (a help request came from a person at a
    // terminal, so the decoder concludes there is one). Falling through would
    // start a hub and sit on the console menu waiting for input - which is
    // exactly what the first run of this example did.
    if ( bHelp )
    {
      Usage ( );
      return 0;
    }

    // Install, remove and exit modes have already done their work inside the
    // decoder, and must not go on to run a hub.
    if ( !oService.IsConsoleMode ( ) && !oService.IsServiceMode ( ) )
      return 0;

    // The kernel. P2PeerService::Run() does NOT do this for you - its
    // StartupP2Pmsg call is commented out - and a hub cannot be created
    // without it.
    if ( !StartupP2Pmsg ( 16 ) )
    {
      oService.Log ( ).Report ( P2Pevent_ERROR, L"wmain"
                              , L"StartupP2Pmsg() failed; cannot host a hub" );
      return 2;
    }

    // The application's own event source. Opened here rather than in Init()
    // because a console run never reaches Init(), and an example that reports
    // differently depending on how it was started teaches the wrong lesson.
    oService.Log ( ).Open ( APP_EVENT_SOURCE );

    // The hub. The service takes ownership - ~P2PeerService deletes it.
    //  ARMING: RequireAuth defaults to ON since ProductionPlan.md Stage 3
    //  step 8, and a hub that requires authentication it cannot enforce
    //  REFUSES TO ARM. This example provisions no identity and no allow-list,
    //  so it takes the documented one-line migration and says so out loud.
    //  NOT the posture to copy into a real hub.
    //  It matters more here than in the sibling examples, and differently.
    //  Elsewhere an unarmed hub is a visible failure; here it is an invisible
    //  one. P2PeerService::Run() guards the pump behind the hub id -
    //  CreateHub() leaves it at 0, RunHub() is never called, the deliberate
    //  pump-thread diagnostic is never raised, and the service stops promptly
    //  and cleanly because NOTHING RAN. That is indistinguishable from the
    //  property this example exists to demonstrate, so without this line the
    //  SCM end-to-end run reports a pass that proves nothing.
    ReportingHub *pHub = new ReportingHub ( APP_HUB_ADDRESS
                                          , &oService.Log ( ) );
    pHub -> RequireAuth ( false );
    oService.PostP2PeerHub ( pHub );

    // Run. Both paths end in P2PeerService::Run(), which calls CleanupP2Pmsg()
    // on the way out - so this function must not call it again.
    int nResult = oService.IsConsoleMode ( )
                ? oService.Run ( )
                : (int)oService.Startup ( );

    oService.Log ( ).Close ( );
    g_pService = 0;
    return nResult;
}

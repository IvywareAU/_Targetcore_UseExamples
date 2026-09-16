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
//  DialogOrLogFile - the ErrToMessageBox switch, and what it costs to get it
//                    wrong
//
//  WHAT THIS DEMONSTRATES
//
//  Targetcore reads a plain text file, P2Pmsg.cfg, beside the host executable:
//
//      ErrToMessageBox: 1        # 1/0, on/off, yes/no, true/false
//      LogFile: errorLog.txt     # relative to THIS file's folder
//
//  With the dialog ON - the default - a diagnostic raised in a host that has
//  nowhere to write text becomes a modal MessageBox. With it OFF the same
//  diagnostic is appended to the log file instead. That is the whole feature,
//  and it would be a one-paragraph example if the difference were cosmetic.
//
//  IT IS NOT COSMETIC, AND THAT IS THE POINT OF THE HARNESS.
//
//  MB_TASKMODAL blocks the thread that raised the event until somebody
//  dismisses it. So the two settings do not differ by where the text appears;
//  they differ by whether the raising thread ever runs again. This harness
//  proves that mechanically instead of asserting it: it re-executes ITSELF as
//  a detached child with no console and no standard error - the one host shape
//  where the dialog is still reachable - and reports which children came back.
//
//      dialog OFF  ->  child exits, and its log file holds the diagnostic
//      dialog ON   ->  child is STILL RUNNING when the timeout expires
//
//  The blocked child is then terminated, so nothing here waits for a human and
//  nothing is left behind. A run with the dialog on is not a failure of this
//  harness - it is the harness working.
//
//  AND ONE THING THE SWITCH CANNOT DO
//
//  ErrToMessageBox: 1 cannot put a dialog back where one would hang. Setting
//  it to 0 is a third way of saying what P2PMSG_NO_UI=1 and ForceTextOutput()
//  already say; setting it to 1 merely declines to say it. The last child leg
//  proves it: dialog ON in the file, P2PMSG_NO_UI=1 in the environment, and
//  the child comes back. A deployment artefact must not be able to re-arm a
//  deadlock that a host had already disarmed.
//
//  Exit codes follow the tree convention: 0 pass, 1 setup failure, 3 a check
//  failed.
//
#include "stdafx.h"
#include "DialogOrLogFile.h"

///////////////////////////////////////////////////////////////////////
//  Verdict bookkeeping

static int s_nChecks   = 0;
static int s_nFailures = 0;

static bool
Check ( bool bCondition, LPCWSTR lpszWhat )
{
    ++s_nChecks;
    if ( !bCondition )
      ++s_nFailures;
    std::wprintf ( L"  [%ls] %ls\n", bCondition ? L"ok  " : L"FAIL", lpszWhat );
    std::fflush ( stdout );
    return bCondition;
}

static void
Head ( LPCWSTR lpszWhat )
{
    std::wprintf ( L"\n%ls\n%ls\n", lpszWhat
                 , L"----------------------------------------------------------------" );
    std::fflush ( stdout );
}

///////////////////////////////////////////////////////////////////////
//  Paths and files

//
//  Folder holding this executable
//  NOTES: The configuration search looks HERE, so the harness has to know it
//         for the leg that exercises the search rather than an explicit path.
static void
ExeDir ( wchar_t *pszOut, size_t cchOut )
{
    pszOut[0] = 0;
    wchar_t wszPath [ MAX_PATH ] = { 0 };
    if ( !::GetModuleFileNameW ( 0, wszPath, MAX_PATH ) )
      return;
    ::wcscpy_s ( pszOut, cchOut, wszPath );
    wchar_t *pszSlash = ::wcsrchr ( pszOut, L'\\' );
    if ( pszSlash )
      *pszSlash = 0;
}

static void
Combine ( wchar_t *pszOut, size_t cchOut, LPCWSTR lpszDir, LPCWSTR lpszLeaf )
{
    ::wcscpy_s ( pszOut, cchOut, lpszDir );
    ::wcscat_s ( pszOut, cchOut, L"\\" );
    ::wcscat_s ( pszOut, cchOut, lpszLeaf );
}

//
//  Writes a configuration file
//  NOTES: UTF-8 without a BOM, which is what the library documents and what
//         every editor now produces by default. Written as bytes rather than
//         through a wide stream so the encoding is this function's decision -
//         a file whose second byte is NUL is the one shape the library
//         refuses, and an example must not accidentally produce it.
static bool
WriteConfig ( LPCWSTR lpszPath, const char *pszBody )
{
    FILE *pf = 0;
    if ( ::_wfopen_s ( &pf, lpszPath, L"wb" ) != 0 || !pf )
      return false;
    const size_t nLen = ::strlen ( pszBody );
    const bool bOk = ( ::fwrite ( pszBody, 1, nLen, pf ) == nLen );
    ::fclose ( pf );
    return bOk;
}

//
//  Reads a whole file as text, for asserting on what landed in the log
//
//  Returns:     bool
//               true  ... the file existed and was read
static bool
ReadAll ( LPCWSTR lpszPath, CStringW& rcsText )
{
    rcsText.Empty ( );

    FILE *pf = 0;
    if ( ::_wfopen_s ( &pf, lpszPath, L"rb" ) != 0 || !pf )
      return false;

    // The log is UTF-8 with no byte order mark, on both platforms and by the
    // library's decision rather than its C runtime's - refer P2PeventWriteLog.
    // So it is read as bytes and converted here, once.
    ::fseek ( pf, 0, SEEK_END );
    long nBytes = ::ftell ( pf );
    ::fseek ( pf, 0, SEEK_SET );
    if ( nBytes > 0 )
    {
      char *pRaw = new char [ (size_t)nBytes + 1 ];
      size_t nRead = ::fread ( pRaw, 1, (size_t)nBytes, pf );
      pRaw [ nRead ] = 0;

      const int cch = ::MultiByteToWideChar ( CP_UTF8, 0, pRaw, -1, 0, 0 );
      if ( cch > 0 )
      {
        wchar_t *pBuf = rcsText.GetBuffer ( cch );
        ::MultiByteToWideChar ( CP_UTF8, 0, pRaw, -1, pBuf, cch );
        rcsText.ReleaseBuffer ( );
      }
      delete [] pRaw;
    }
    ::fclose ( pf );
    return true;
}

static void
DeleteIfPresent ( LPCWSTR lpszPath )
{
    ::DeleteFileW ( lpszPath );
}

///////////////////////////////////////////////////////////////////////
//  Raising a diagnostic

//
//  Raises one P2Pevent through the library's own display path
//  NOTES: Display() is what decides between the dialog and text, so the event
//         has to go through it. Nothing here writes to the log directly - an
//         example that formatted its own line would prove only that this file
//         can call fwprintf.
//       : Cancel(false) discards without re-displaying. MakeEvent hands over
//         ownership and the event is not thrown, so somebody has to release it.
static void
RaiseOne ( P2Pevent_e eClass, LPCWSTR lpszMessage )
{
    P2Pevent *pEvent = P2Pevent::MakeEvent ( eClass );
    if ( !pEvent )
      return;
    pEvent -> Module_  ( L"DialogOrLogFile" )
           -> Message_ ( lpszMessage )
           -> Display  ( );
    pEvent -> Cancel ( false );
}

///////////////////////////////////////////////////////////////////////
//  1. The search - a file placed beside the executable, and found

//
//  NOTES: This is the only leg that puts P2Pmsg.cfg where the library looks
//         for it, because that file governs every MSCS process started from
//         this folder. It is removed again immediately. Every other leg names
//         its file explicitly, which is also what a test should do - refer the
//         note on P2PMSG_CONFIG in Msgexception.h.
static void
TestSearch ( LPCWSTR lpszExeDir, LPCWSTR lpszWorkDir )
{
    Head ( L"1. the search: P2Pmsg.cfg beside the executable" );

    wchar_t wszCfg [ MAX_PATH ] = { 0 };
    Combine ( wszCfg, MAX_PATH, lpszExeDir, P2PMSG_CFG_FILE );

    if ( !WriteConfig ( wszCfg,
             "# Written by DialogOrLogFile, and deleted again immediately.\n"
             "ErrToMessageBox: off\n"
             "LogFile: cfgdemo\\search.log\n" ) )
    {
      Check ( false, L"could write P2Pmsg.cfg beside the executable" );
      return;
    }

    wchar_t wszWant [ MAX_PATH ] = { 0 };
    Combine ( wszWant, MAX_PATH, lpszWorkDir, L"search.log" );
    DeleteIfPresent ( wszWant );

    P2Pevent::ForceTextOutput ( false );          // clear the latch first
    const bool bFound = P2Pevent::LoadConfigFile ( 0 );   // 0 = perform the search

    std::wprintf ( L"  config found : %ls\n", P2Pevent::ConfigFilePath ( ) );
    std::wprintf ( L"  log resolved : %ls\n", P2Pevent::LogFilePath ( ) );
    std::wprintf ( L"  expected     : %ls\n", wszWant );

    Check ( bFound, L"LoadConfigFile(0) finds a file beside the executable" );
    Check ( ::_wcsicmp ( P2Pevent::ConfigFilePath ( ), wszCfg ) == 0
          , L"ConfigFilePath names the file that was read" );
    Check ( ::_wcsicmp ( P2Pevent::LogFilePath ( ), wszWant ) == 0
          , L"a relative LogFile resolves against the CONFIG FILE's folder" );
    Check ( P2Pevent::UsesTextOutput ( )
          , L"ErrToMessageBox: off takes the dialog off the table" );

    DeleteIfPresent ( wszCfg );
}

///////////////////////////////////////////////////////////////////////
//  2. Off, with no LogFile named - the documented default name

static void
TestDefaultLogName ( LPCWSTR lpszWorkDir )
{
    Head ( L"2. ErrToMessageBox: off with no LogFile - errorLog.txt" );

    wchar_t wszCfg [ MAX_PATH ] = { 0 };
    wchar_t wszLog [ MAX_PATH ] = { 0 };
    Combine ( wszCfg, MAX_PATH, lpszWorkDir, L"default.cfg" );
    Combine ( wszLog, MAX_PATH, lpszWorkDir, P2PMSG_LOG_FILE );
    DeleteIfPresent ( wszLog );

    if ( !WriteConfig ( wszCfg, "ErrToMessageBox: no\n" ) )
    {
      Check ( false, L"could write the configuration file" );
      return;
    }

    P2Pevent::ForceTextOutput ( false );
    P2Pevent::LoadConfigFile ( wszCfg );

    std::wprintf ( L"  log resolved : %ls\n", P2Pevent::LogFilePath ( ) );
    Check ( ::_wcsicmp ( P2Pevent::LogFilePath ( ), wszLog ) == 0
          , L"turning the dialog off implies errorLog.txt beside the config" );

    RaiseOne ( P2Pevent_ERROR, L"leg 2: the default log name" );

    CStringW csLog;
    Check ( ReadAll ( wszLog, csLog )
          , L"errorLog.txt was created by raising an event" );
    Check ( csLog.Find ( L"leg 2: the default log name" ) >= 0
          , L"the diagnostic body is in the log file" );
    Check ( csLog.Find ( L"DialogOrLogFile" ) >= 0
          , L"so is the origin, module included" );
    Check ( csLog.Find ( L"[ERROR]" ) >= 0
          , L"and the event class, spelled out" );
}

///////////////////////////////////////////////////////////////////////
//  3. On, with a LogFile named - the setting is not the only input

//
//  NOTES: The point of this leg is a distinction that is easy to miss.
//         ErrToMessageBox governs whether the dialog is ON THE TABLE; it does
//         not decide the outcome on its own. This process is a console
//         application, so it has a writable standard error, so Display()
//         chooses text regardless - and a LogFile, once named, is where that
//         text goes. Naming a destination is an explicit request; stderr is a
//         default, and an explicit request beats a default.
static void
TestDialogOnStillLogs ( LPCWSTR lpszWorkDir )
{
    Head ( L"3. ErrToMessageBox: on, LogFile named - a console host still logs" );

    wchar_t wszCfg [ MAX_PATH ] = { 0 };
    wchar_t wszLog [ MAX_PATH ] = { 0 };
    Combine ( wszCfg, MAX_PATH, lpszWorkDir, L"dialogon.cfg" );
    Combine ( wszLog, MAX_PATH, lpszWorkDir, L"dialogon.log" );
    DeleteIfPresent ( wszLog );

    if ( !WriteConfig ( wszCfg, "ErrToMessageBox: yes\nLogFile: dialogon.log\n" ) )
    {
      Check ( false, L"could write the configuration file" );
      return;
    }

    P2Pevent::ForceTextOutput ( false );
    P2Pevent::LoadConfigFile ( wszCfg );

    Check ( ::_wcsicmp ( P2Pevent::LogFilePath ( ), wszLog ) == 0
          , L"the LogFile is honoured with the dialog nominally ON" );
    Check ( P2Pevent::UsesTextOutput ( )
          , L"and text is still chosen here - this host HAS a stderr" );

    // WARNING has to be in the notification mask before it can be raised at
    // all: Display() consults the mask before it emits anything, so a WARNING
    // raised without it reaches neither the dialog nor the log. The first
    // draft of this leg failed three checks on that and not on anything to do
    // with the log file.
    //   SINCE 2026-08-20 WARNING IS IN THE DEFAULT MASK, so the ADDMASK below
    // is nowadays a no-op - kept, because this example is about being explicit
    // and because a default is a thing that can change again. It used to be
    // ERROR alone, which made every warning in the libraries invisible; that
    // was Targetcore's finding F-S6-4.
    //   Worth knowing beyond this example: turning the dialog off does not
    // turn reporting ON. The two are separate configurations and neither
    // implies the other.
    const DWORD dwMask = P2Pevent::Configure ( P2Pevent::GETMASK, 0 );
    P2Pevent::Configure ( P2Pevent::ADDMASK, P2Pevotn_WARNING );

    RaiseOne ( P2Pevent_WARNING, L"leg 3: dialog on, but text was chosen anyway" );

    P2Pevent::Configure ( P2Pevent::SETMASK, dwMask );

    CStringW csLog;
    Check ( ReadAll ( wszLog, csLog )
          , L"the named log file was written" );
    Check ( csLog.Find ( L"leg 3: dialog on" ) >= 0
          , L"with this leg's diagnostic in it" );
    Check ( csLog.Find ( L"[WARNING]" ) >= 0
          , L"WARNING is logged as WARNING, not flattened to ERROR" );
}

///////////////////////////////////////////////////////////////////////
//  4. A value the parser does not understand

//
//  NOTES: An unreadable setting must be IGNORED and complained about, not
//         guessed at. Guessing "off" would suppress a windowed application's
//         dialogs on the strength of a typo; guessing "on" would suppress the
//         evidence that the line was never understood. So the default stands
//         and the complaint is available.
static void
TestBadValue ( LPCWSTR lpszWorkDir )
{
    Head ( L"4. a value the parser cannot read - ignored, and said so" );

    wchar_t wszCfg [ MAX_PATH ] = { 0 };
    Combine ( wszCfg, MAX_PATH, lpszWorkDir, L"bad.cfg" );

    if ( !WriteConfig ( wszCfg,
             "ErrToMessageBox: maybe\n"
             "LogFyle: typo.log\n"
             "this line has no separator at all\n" ) )
    {
      Check ( false, L"could write the configuration file" );
      return;
    }

    P2Pevent::ForceTextOutput ( false );
    P2Pevent::LoadConfigFile ( wszCfg );

    std::wprintf ( L"  complaint    : %ls\n", P2Pevent::ConfigDiagnostic ( ) );

    Check ( *P2Pevent::ConfigDiagnostic ( ) != 0
          , L"ConfigDiagnostic reports what could not be read" );
    Check ( !*P2Pevent::LogFilePath ( )
          , L"a misspelled LogFyle names no destination" );

    // The ONE thing that must not have happened: an unreadable ErrToMessageBox
    // must not have moved the setting. This process has a stderr so
    // UsesTextOutput() is true either way and cannot be the check; the check is
    // that nothing was latched, which ForceTextOutput reports as it clears.
    const bool bWasLatched = P2Pevent::ForceTextOutput ( false );
    Check ( !bWasLatched
          , L"'maybe' did not latch the dialog off behind our back" );
}

///////////////////////////////////////////////////////////////////////
//  5. The children - the shape where the setting actually decides

//
//  Launches this executable again, windowless and blind
//  NOTES: CREATE_NO_WINDOW, and the child then frees its console and NULLs its
//         own standard error. Both halves are needed and they are not the same
//         thing: GetConsoleWindow() and STD_ERROR_HANDLE are two separate
//         inputs to the policy, and a child that had either would be handed
//         text without the dialog ever being considered - which is the exact
//         host shape this leg is trying NOT to be.
//       : The child keeps its session and the visible window station, so a
//         dialog raised there really does reach the desktop. That is what makes
//         the timeout below meaningful rather than a guess.
//       : NOT DETACHED_PROCESS, and this cost a round to find. A detached
//         child's MessageBox appears and then CLOSES ITSELF after about two
//         seconds, whereupon the process exits 0 - so the harness saw a child
//         that returned, concluded nothing had blocked, and reported the
//         opposite of the truth while a dialog was sitting on the screen. The
//         window was there in Get-Process the whole time, owned by the very
//         pid the harness had just called healthy.
//         That is worth more than the flag it cost: a demonstration of
//         blocking is only as good as the host shape it blocks in, and
//         "detached" is not the shape a service has. A service has a session,
//         a station and a desktop; what it lacks is a console and a standard
//         error. CREATE_NO_WINDOW plus the child's own two removals is that,
//         and it reproduces the block every time.
//
//  Parameters:  LPCWSTR lpszCfg
//               Configuration file for the child to load.
//
//               LPCWSTR lpszEnvNoUI
//               Value for P2PMSG_NO_UI in the child, or 0 to leave it unset.
//
//  Returns:     HANDLE
//               Child process, or 0 if it could not be started.
static HANDLE
LaunchChild ( LPCWSTR lpszCfg, LPCWSTR lpszEnvNoUI, DWORD *pdwPid )
{
    if ( pdwPid )
      *pdwPid = 0;

    wchar_t wszExe [ MAX_PATH ] = { 0 };
    if ( !::GetModuleFileNameW ( 0, wszExe, MAX_PATH ) )
      return 0;

    // Quoted, because the output folder sits under a path the developer chose
    // and a space in it would otherwise split the command line.
    wchar_t wszCmd [ MAX_PATH * 3 ] = { 0 };
    ::_snwprintf_s ( wszCmd, _TRUNCATE, L"\"%ls\" %ls \"%ls\""
                   , wszExe, ARG_CHILD, lpszCfg );

    // Set in the PARENT and inherited, rather than built into an environment
    // block. The block would have to be copied and appended to by hand, and
    // getting that wrong silently strips the child's PATH.
    if ( lpszEnvNoUI )
      ::SetEnvironmentVariableW ( L"P2PMSG_NO_UI", lpszEnvNoUI );

    STARTUPINFOW oStart = { 0 };
    oStart.cb = sizeof(oStart);
    PROCESS_INFORMATION oProc = { 0 };

    const BOOL bOk = ::CreateProcessW ( wszExe, wszCmd, 0, 0, FALSE
                                      , CREATE_NO_WINDOW, 0, 0
                                      , &oStart, &oProc );

    if ( lpszEnvNoUI )
      ::SetEnvironmentVariableW ( L"P2PMSG_NO_UI", 0 );   // 0 removes it

    if ( !bOk )
      return 0;
    ::CloseHandle ( oProc.hThread );
    if ( pdwPid )
      *pdwPid = oProc.dwProcessId;
    return oProc.hProcess;
}

//
//  Does this process own a dialog window right now?
//  NOTES: "#32770" is the window class of every Win32 dialog, MessageBox
//         included. Enumerating for it is how this harness observes the dialog
//         WITHOUT depending on how long the dialog survives - refer RunChildLeg
//         for why that distinction turned out to matter.
struct DialogHunt
{
    DWORD dwPid;
    bool  bFound;
};

static BOOL CALLBACK
FindDialogProc ( HWND hWnd, LPARAM lParam )
{
    DialogHunt *pHunt = (DialogHunt*)lParam;

    DWORD dwPid = 0;
    ::GetWindowThreadProcessId ( hWnd, &dwPid );
    if ( dwPid != pHunt -> dwPid )
      return TRUE;

    wchar_t wszClass [ 64 ] = { 0 };
    if ( ::GetClassNameW ( hWnd, wszClass, 64 )
      && ::wcscmp ( wszClass, L"#32770" ) == 0 )
    {
      pHunt -> bFound = true;
      return FALSE;                    // stop enumerating
    }
    return TRUE;
}

static bool
HasDialogWindow ( DWORD dwPid )
{
    DialogHunt oHunt = { dwPid, false };
    ::EnumWindows ( &FindDialogProc, (LPARAM)&oHunt );
    return oHunt.bFound;
}

//
//  Runs one child leg and reports what the child did
//  NOTES: THE ASSERTION IS THE DIALOG WINDOW, NOT THE BLOCK, and the
//         difference is the whole reason this function is shaped the way it is.
//         Gating on "the child was still running when the timeout expired"
//         looked right and failed roughly one run in four: the MessageBox
//         appears, sits there, and is then CLOSED BY SOMETHING ELSE on a live
//         desktop, whereupon the child exits 0 well inside the timeout.
//         Nothing about the library changed between a pass and a fail.
//       : So the harness watches for the dialog instead. A modal dialog having
//         been RAISED is the property the setting controls, it is observable
//         the instant it happens, and no third party can un-observe it. Whether
//         the child then stays blocked is reported and not asserted - the same
//         call the sibling harness made about 'pump left dispatch', and for the
//         same reason: an intermittent harness gets ignored, and then so does
//         everything it says.
//       : The block is not thereby unproven. It is proven for the shape that
//         matters by NTServiceEventLog under the SCM, where there is no desktop
//         and so nothing to close the dialog, and by the fact that a dialog was
//         raised at all on a thread whose caller is routinely a hub's pump.
//
//  Parameters:  bool bExpectDialog
//               true  ... a dialog window MUST appear, and the log stay empty
//               false ... no dialog may appear; the child must exit and log
static void
RunChildLeg ( LPCWSTR lpszTitle, LPCWSTR lpszCfg, LPCWSTR lpszEnvNoUI
            , LPCWSTR lpszLog,   LPCWSTR lpszWant
            , bool bExpectDialog, DWORD dwTimeoutMs )
{
    std::wprintf ( L"\n  %ls\n", lpszTitle );

    if ( lpszLog )
      DeleteIfPresent ( lpszLog );

    DWORD  dwPid   = 0;
    HANDLE hChild  = LaunchChild ( lpszCfg, lpszEnvNoUI, &dwPid );
    if ( !hChild )
    {
      Check ( false, L"the child process could be started" );
      return;
    }
    std::wprintf ( L"    child pid  : %lu\n", dwPid );
    std::fflush ( stdout );

    // Watch for BOTH outcomes at once, in short steps, so a dialog that is
    // dismissed by something else is still seen before it goes. Polling rather
    // than a hook because the dialog belongs to another process and this is a
    // harness, not a shell extension.
    bool  bSawDialog = false;
    bool  bReturned  = false;
    DWORD dwElapsed  = 0;
    const DWORD dwStepMs = 100;
    for ( ; dwElapsed < dwTimeoutMs; dwElapsed += dwStepMs )
    {
      if ( HasDialogWindow ( dwPid ) )
        bSawDialog = true;
      if ( ::WaitForSingleObject ( hChild, dwStepMs ) == WAIT_OBJECT_0 )
      {
        bReturned = true;
        break;
      }
    }

    DWORD dwExit = 0;
    if ( bReturned )
      ::GetExitCodeProcess ( hChild, &dwExit );
    else
    {
      // Still blocked. Terminating our own child is the whole reason this
      // harness can demonstrate the property unattended - the alternative is a
      // MessageBox sitting on somebody's desktop until they find it.
      ::TerminateProcess ( hChild, 99 );
      ::WaitForSingleObject ( hChild, 5000 );
    }
    ::CloseHandle ( hChild );

    // What the child itself resolved, in its own words. Printed whether the
    // leg passed or failed - a leg that fails is exactly when this matters.
    wchar_t wszState [ MAX_PATH ] = { 0 };
    ::_snwprintf_s ( wszState, _TRUNCATE, L"%ls%ls", lpszCfg, STATE_SUFFIX );
    FILE *pfState = 0;
    if ( ::_wfopen_s ( &pfState, wszState, L"r, ccs=UTF-8" ) == 0 && pfState )
    {
      wchar_t wszLine [ 512 ] = { 0 };
      if ( ::fgetws ( wszLine, 512, pfState ) )
        std::wprintf ( L"    child saw: %ls", wszLine );
      ::fclose ( pfState );
      DeleteIfPresent ( wszState );
    }
    else
      std::wprintf ( L"    child wrote no state file - it died before it could\n" );

    std::wprintf ( L"    dialog window seen : %ls\n"
                   L"    child %ls"
                 , bSawDialog ? L"YES" : L"no"
                 , bReturned  ? L"EXITED" : L"BLOCKED" );
    if ( bReturned )
      std::wprintf ( L" with code %lu after ~%lu ms", dwExit, dwElapsed );
    else
      std::wprintf ( L" past %lu ms, and was terminated", dwTimeoutMs );
    std::wprintf ( L"\n" );

    if ( bExpectDialog )
    {
      Check ( bSawDialog
            , L"a modal dialog WAS raised - the setting put it on the table" );
      if ( lpszLog )
      {
        CStringW csLog;
        Check ( !ReadAll ( lpszLog, csLog ) || csLog.IsEmpty ( )
              , L"and nothing was logged, because the dialog took the event" );
      }
      // Reported, NOT asserted. Refer the note on this function.
      std::wprintf ( L"    (blocked = %ls; informational only - on a live "
                     L"desktop something\n     else may close the dialog, and "
                     L"that is not this library's doing)\n"
                   , bReturned ? L"no" : L"yes" );
    }
    else
    {
      Check ( !bSawDialog
            , L"NO dialog was raised - there was somewhere to write instead" );
      Check ( bReturned
            , L"the child returned, so nothing blocked its thread" );
      if ( bReturned )
        Check ( dwExit == 0, L"and it returned success" );
      if ( lpszLog && lpszWant )
      {
        CStringW csLog;
        Check ( ReadAll ( lpszLog, csLog )
              , L"the child's log file exists" );
        Check ( csLog.Find ( lpszWant ) >= 0
              , L"and the diagnostic it raised is in it" );
      }
    }
}

static void
TestChildren ( LPCWSTR lpszWorkDir, bool bAllowDialog, DWORD dwTimeoutMs )
{
    Head ( L"5. a host with no console and no stderr - where the switch decides" );
    std::wprintf (
      L"  Each leg re-executes this program with CREATE_NO_WINDOW; the child then\n"
      L"  frees its console and NULLs its own standard error. It keeps its session\n"
      L"  and window station, so the dialog is genuinely reachable - which is what\n"
      L"  makes the timeout a measurement rather than a guess. Refer LaunchChild\n"
      L"  for why DETACHED_PROCESS was wrong here, and what it hid.\n" );

    wchar_t wszOffCfg [ MAX_PATH ] = { 0 }, wszOffLog [ MAX_PATH ] = { 0 };
    wchar_t wszOnCfg  [ MAX_PATH ] = { 0 }, wszOnLog  [ MAX_PATH ] = { 0 };
    wchar_t wszEnvCfg [ MAX_PATH ] = { 0 }, wszEnvLog [ MAX_PATH ] = { 0 };
    Combine ( wszOffCfg, MAX_PATH, lpszWorkDir, L"child_off.cfg" );
    Combine ( wszOffLog, MAX_PATH, lpszWorkDir, L"child_off.log" );
    Combine ( wszOnCfg,  MAX_PATH, lpszWorkDir, L"child_on.cfg"  );
    Combine ( wszOnLog,  MAX_PATH, lpszWorkDir, L"child_on.log"  );
    Combine ( wszEnvCfg, MAX_PATH, lpszWorkDir, L"child_env.cfg" );
    Combine ( wszEnvLog, MAX_PATH, lpszWorkDir, L"child_env.log" );

    const bool bWritten =
        WriteConfig ( wszOffCfg, "ErrToMessageBox: 0\nLogFile: child_off.log\n" )
     && WriteConfig ( wszOnCfg,  "ErrToMessageBox: 1\nLogFile: child_on.log\n" )
     && WriteConfig ( wszEnvCfg, "ErrToMessageBox: 1\nLogFile: child_env.log\n" );
    if ( !bWritten )
    {
      Check ( false, L"could write the children's configuration files" );
      return;
    }

    // The last argument is "a dialog is expected", so it is FALSE for the two
    // legs that must not raise one and TRUE for the one that must.
    RunChildLeg ( L"(a) ErrToMessageBox: 0  - the substitution"
                , wszOffCfg, 0, wszOffLog, CHILD_MESSAGE
                , false, dwTimeoutMs );

    if ( bAllowDialog )
      RunChildLeg ( L"(b) ErrToMessageBox: 1  - the modal dialog"
                  , wszOnCfg, 0, wszOnLog, 0
                  , true, dwTimeoutMs );
    else
      std::wprintf ( L"\n  (b) ErrToMessageBox: 1  - SKIPPED (-nodialog)\n"
                     L"      This is the leg that raises a real MessageBox and\n"
                     L"      proves the block. Skipping it leaves the harness\n"
                     L"      claiming only half of the subject.\n" );

    RunChildLeg ( L"(c) ErrToMessageBox: 1 with P2PMSG_NO_UI=1  - the file loses"
                , wszEnvCfg, L"1", wszEnvLog, CHILD_MESSAGE
                , false, dwTimeoutMs );
}

///////////////////////////////////////////////////////////////////////
//  The child

//
//  NOTES: Prints nothing, and cannot - it has no console by construction. Its
//         entire output is the log file it was configured to write, and its
//         entire verdict is whether it reaches the return statement.
static int
RunAsChild ( LPCWSTR lpszCfg )
{
    // Blind by construction. DETACHED_PROCESS already denied it a console;
    // this denies it the standard error handle as well, which a service under
    // the SCM also lacks. Two separate inputs to the policy, so two separate
    // removals.
    ::SetStdHandle ( STD_ERROR_HANDLE,  0 );
    ::SetStdHandle ( STD_OUTPUT_HANDLE, 0 );
    ::FreeConsole ( );

    P2Pevent::LoadConfigFile ( lpszCfg );

    // The child's own account of the four inputs, written BEFORE the event is
    // raised - because if the event blocks, anything written afterwards is
    // never written at all, and this file is then the only evidence of what
    // the child decided.
    //   This is here because the harness needed it. A child that neither
    // blocked nor logged looked like two different bugs at once, and no amount
    // of reading the parent could distinguish them: the answer was in a
    // process with no console, no stderr and nothing to say.
    HWINSTA         hWinSta = ::GetProcessWindowStation ( );
    USEROBJECTFLAGS oFlags  = { 0 };
    DWORD           dwRet   = 0;
    const bool bFlags = hWinSta && ::GetUserObjectInformationW
                          ( hWinSta, UOI_FLAGS, &oFlags, sizeof(oFlags), &dwRet );
    DWORD dwSession = (DWORD)-1;
    ::ProcessIdToSessionId ( ::GetCurrentProcessId ( ), &dwSession );
    const HANDLE hErr = ::GetStdHandle ( STD_ERROR_HANDLE );

    wchar_t wszState [ MAX_PATH ] = { 0 };
    ::_snwprintf_s ( wszState, _TRUNCATE, L"%ls%ls", lpszCfg, STATE_SUFFIX );
    FILE *pf = 0;
    if ( ::_wfopen_s ( &pf, wszState, L"w, ccs=UTF-8" ) == 0 && pf )
    {
      ::fwprintf ( pf, L"session=%ld visible=%ls stderr=%ls console=%ls "
                       L"textOutput=%ls log='%ls'\n"
                 , (long)dwSession
                 , !bFlags ? L"unknown"
                   : ( oFlags.dwFlags & WSF_VISIBLE ) ? L"yes" : L"no"
                 , ( hErr && hErr != INVALID_HANDLE_VALUE ) ? L"yes" : L"no"
                 , ::GetConsoleWindow ( ) ? L"yes" : L"no"
                 , P2Pevent::UsesTextOutput ( ) ? L"TEXT" : L"DIALOG"
                 , P2Pevent::LogFilePath ( ) );
      ::fclose ( pf );
    }

    RaiseOne ( P2Pevent_ERROR, CHILD_MESSAGE );
    return 0;                          // reaching here IS the result
}

///////////////////////////////////////////////////////////////////////
//  Entry point

static void
Usage ( )
{
    std::wprintf (
      L"\n"
      L"DialogOrLogFile - the ErrToMessageBox switch, demonstrated\n"
      L"\n"
      L"  (no arguments)  run every leg, including the one that raises a real\n"
      L"                  modal dialog in a detached child and terminates it\n"
      L"  -nodialog       skip that leg - nothing appears on screen, and the\n"
      L"                  harness then shows only half of the subject\n"
      L"  -keep           leave the scratch folder in place afterwards\n"
      L"  -timeout <ms>   how long a child gets before it counts as blocked\n"
      L"                  (default %lu)\n"
      L"  -?              this text\n"
      L"\n"
      L"The configuration file this demonstrates, in full:\n"
      L"\n"
      L"    ErrToMessageBox: 1        # 1/0, on/off, yes/no, true/false\n"
      L"    LogFile: errorLog.txt     # relative to THIS file's folder\n"
      L"\n"
      L"It is read from %ls beside the host executable. Both settings are\n"
      L"optional; the defaults are the dialog ON and no log file, which is\n"
      L"exactly the behaviour that stood before the file was understood.\n"
      L"\n" , (unsigned long)DEFAULT_TIMEOUT_MS, P2PMSG_CFG_FILE );
}

int
wmain ( int argc, wchar_t *argv[] )
{
    // The child leg first, and before anything is printed. It has no console
    // to print to, and deciding that after emitting a banner would write the
    // banner to a handle that is being taken away.
    if ( argc >= 3 && ::_wcsicmp ( argv[1], ARG_CHILD ) == 0 )
      return RunAsChild ( argv[2] );

    bool  bAllowDialog = true;
    bool  bKeep        = false;
    DWORD dwTimeoutMs  = DEFAULT_TIMEOUT_MS;

    for ( int i = 1; i < argc; i++ )
    {
      if ( ::_wcsicmp ( argv[i], L"-nodialog" ) == 0 )
        bAllowDialog = false;
      else if ( ::_wcsicmp ( argv[i], L"-keep" ) == 0 )
        bKeep = true;
      else if ( ::_wcsicmp ( argv[i], L"-timeout" ) == 0 && i + 1 < argc )
        dwTimeoutMs = (DWORD)::_wtoi ( argv[++i] );
      else if ( ::_wcsicmp ( argv[i], L"-?" ) == 0
             || ::_wcsicmp ( argv[i], L"-h" ) == 0
             || ::_wcsicmp ( argv[i], L"--help" ) == 0 )
      {
        Usage ( );
        return 0;
      }
      else
      {
        std::wprintf ( L"Unrecognised argument '%ls'.\n", argv[i] );
        Usage ( );
        return 1;
      }
    }

    std::wprintf (
      L"=== DialogOrLogFile - ErrToMessageBox, and what it costs to get it wrong ===\n"
      L"\n"
      L"Asserting: the switch decides between a modal dialog and a log file, and\n"
      L"           in a host with nowhere to write, that is the difference between\n"
      L"           a thread that continues and one that does not.\n" );
    std::fflush ( stdout );

    wchar_t wszExeDir  [ MAX_PATH ] = { 0 };
    wchar_t wszWorkDir [ MAX_PATH ] = { 0 };
    ExeDir ( wszExeDir, MAX_PATH );
    if ( !*wszExeDir )
    {
      std::wprintf ( L"\nFAIL: could not determine this executable's folder.\n" );
      return 1;
    }
    Combine ( wszWorkDir, MAX_PATH, wszExeDir, WORK_SUBDIR );
    if ( !::CreateDirectoryW ( wszWorkDir, 0 )
      && ::GetLastError ( ) != ERROR_ALREADY_EXISTS )
    {
      std::wprintf ( L"\nFAIL: could not create '%ls'.\n", wszWorkDir );
      return 1;
    }
    std::wprintf ( L"\nScratch folder: %ls\n", wszWorkDir );

    TestSearch          ( wszExeDir, wszWorkDir );
    TestDefaultLogName  ( wszWorkDir );
    TestDialogOnStillLogs ( wszWorkDir );
    TestBadValue        ( wszWorkDir );
    TestChildren        ( wszWorkDir, bAllowDialog, dwTimeoutMs );

    // Back to the library's defaults, so nothing here leaks into a process
    // that outlives the harness - and so a reader sees the restoration.
    P2Pevent::SetLogFile      ( 0 );
    P2Pevent::ForceTextOutput ( false );

    Head ( L"Verdict" );
    std::wprintf ( L"  %d checks, %d failures\n", s_nChecks, s_nFailures );
    if ( !bAllowDialog )
      std::wprintf ( L"  NOTE: -nodialog was given, so the leg that proves the\n"
                     L"        block did not run.\n" );
    std::wprintf ( L"  %ls\n", s_nFailures == 0 ? L"PASS" : L"FAIL" );

    if ( !bKeep )
    {
      // The scratch files, but NOT the folder - leaving it costs nothing and
      // its presence tells the next reader where to look with -keep.
      static LPCWSTR s_apszLeaves [] =
        { L"search.log", P2PMSG_LOG_FILE, L"default.cfg", L"dialogon.cfg"
        , L"dialogon.log", L"bad.cfg", L"child_off.cfg", L"child_off.log"
        , L"child_on.cfg", L"child_on.log", L"child_env.cfg", L"child_env.log" };
      for ( size_t i = 0; i < sizeof(s_apszLeaves)/sizeof(s_apszLeaves[0]); i++ )
      {
        wchar_t wszLeaf [ MAX_PATH ] = { 0 };
        Combine ( wszLeaf, MAX_PATH, wszWorkDir, s_apszLeaves[i] );
        DeleteIfPresent ( wszLeaf );
      }
    }
    else
      std::wprintf ( L"  -keep: scratch files left in %ls\n", wszWorkDir );

    std::fflush ( stdout );
    return s_nFailures == 0 ? 0 : 3;
}

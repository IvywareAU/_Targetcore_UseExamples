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
// PipeMeshTest.cpp
//
// SINGLE-PROCESS, TWO-HUB named-pipe connectivity probe for Targetcore.
//
// Question this harness answers:
//   Can two P2PeerHub's living in ONE process connect to each other over a
//   Windows NAMED PIPE (P2PeerConPipe) and complete the Targetcore login
//   handshake -- OR does it trip the same failure the loopback-TCP path did
//   (ASSERT(pCon==nullptr) at P2Pwin32.cpp:3844, no On_ConLoginAck)?
//
// It mirrors AlexTest exactly, with two differences:
//   1. BOTH the server hub and the client hub run in this one process, each
//      on its own SpawnHub() pump thread.
//   2. The transport is P2PeerConPipe (named pipe) instead of P2PeerConWsa.
//
// Verdict is reported by process EXIT CODE (so a headless run is unambiguous):
//   0 = SUCCESS   : client got On_ConLoginAck AND server received the BCast.
//   2 = ASSERT    : an MFC/CRT assertion fired (banner printed; if it is
//                   P2Pwin32.cpp:3844 the pipe path fails the same way TCP did).
//   3 = TIMEOUT   : no assert, but handshake/delivery did not complete in time.
//   1 = SETUP     : startup/factory failure.

#include "stdafx.h"
#include "PipeMeshTest.h"

#include <io.h>
#include <fcntl.h>
#include <crtdbg.h>

#include "P2Pwin32.h"
#include "P2PeerHub.h"
#include "P2PeerConPipe.h"
#include "P2PeerMsg.h"
#include "Msgexception.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// MFC requires exactly one CWinApp instance per executable
CWinApp theApp;

// -------------------------------------------------------------------------
// Mesh configuration
// -------------------------------------------------------------------------
static LPCTSTR            kPipeName   = _T("\\\\.\\pipe\\P2PmeshProbe");
static const P2PaddrSTR   kServerAddr = L"PipeMesh.Server";
static const P2PaddrSTR   kClientAddr = L"PipeMesh.Client";

// Signalled by the SERVER hub once it receives the client's BCast.
static HANDLE g_hDoneEvent = NULL;

// -------------------------------------------------------------------------
// Timestamped, flushed milestone log.
// -------------------------------------------------------------------------
static void LogAt(LPCWSTR lpszRole, LPCWSTR lpszMsg)
{
    SYSTEMTIME st; GetLocalTime(&st);
    wprintf(L"[%02d:%02d:%02d.%03d tid=%lu %s] %s\n",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            GetCurrentThreadId(), lpszRole, lpszMsg);
    fflush(stdout);
}

// -------------------------------------------------------------------------
// Assertion trap.
//
// A debug MFC/CRT ASSERT would normally pop a modal dialog and hang a
// headless run. We route reports to a hook that prints a clear banner
// (including file:line, so a 3844 hit is obvious) and terminates the whole
// process with exit code 2 -- turning "it asserted" into a deterministic,
// captured verdict instead of a hang or a cascade crash on a bad pointer.
// -------------------------------------------------------------------------
static int __cdecl AssertReportHook(int nReportType, char* szMsg, int* pnRet)
{
    if (nReportType == _CRT_ASSERT)
    {
        fflush(stdout);
        fprintf(stderr,
                "\n=== ASSERT TRIPPED (single-process pipe mesh) ===\n%s\n"
                "=== interpreting as: named-pipe in-process connect FAILED ===\n",
                szMsg ? szMsg : "(no message)");
        fflush(stderr);
        if (pnRet) *pnRet = 0;   // don't invoke the debugger
        ExitProcess(2);
    }
    return FALSE;                // let other report types flow normally
}

// =========================================================================
// PipeMeshHub
//   Same instrumented P2PeerHub subclass shape AlexTest uses: every
//   connection-lifecycle stage is traced, the client posts a BCast once its
//   login is acked, and the server signals g_hDoneEvent when the BCast lands.
// =========================================================================
class PipeMeshHub : public P2PeerHub
{
public:
    PipeMeshHub(P2PaddrSTR strAddr, bool bServer)
        : P2PeerHub(strAddr)
        , m_bServer(bServer)
        , m_bSent(false)
    {}
    virtual ~PipeMeshHub() {}

protected:
    virtual msgRESULT On_P2PeerBCast(P2PeerMsg* pMsg) override
    {
        PrintMessage(L"BCast", pMsg);
        return msgHANDLED;
    }

    virtual msgRESULT On_P2PeerUCast(P2PeerMsg* pMsg) override
    {
        PrintMessage(L"UCast", pMsg);
        return msgHANDLED;
    }

    virtual conRESULT On_ConLoginAck(P2PeerCon*   pCon,
                                     P2PaddrSTR   strThisP2Paddr,
                                     P2PaddrSTR   strThatP2Paddr,
                                     const void*  pvLoginAck,
                                     P2Psize_t    iSize) override
    {
        Trace(L"On_ConLoginAck", pCon);
        conRESULT result = P2PeerHub::On_ConLoginAck(
                               pCon, strThisP2Paddr, strThatP2Paddr, pvLoginAck, iSize);

        if (!m_bServer && !m_bSent)
        {
            wprintf(L"[CLIENT] Login ack from '%s' - pipe connection ready.\n",
                    strThatP2Paddr);
            PostTestMessage();
            m_bSent = true;
        }
        return result;
    }

    // Connection-lifecycle traces -----------------------------------------
    virtual conRESULT On_ConStartup(P2PeerCon* pCon) override
    { Trace(L"On_ConStartup", pCon); return P2PeerHub::On_ConStartup(pCon); }

    virtual conRESULT On_ConConnect(P2PeerCon* pCon) override
    { Trace(L"On_ConConnect", pCon); return P2PeerHub::On_ConConnect(pCon); }

    virtual conRESULT On_ConAccept(P2PeerCon* pCon) override
    { Trace(L"On_ConAccept", pCon); return P2PeerHub::On_ConAccept(pCon); }

    virtual conRESULT On_ConListen(P2PeerCon* pCon) override
    { Trace(L"On_ConListen", pCon); return P2PeerHub::On_ConListen(pCon); }

    virtual conRESULT On_ConLogin(P2PeerCon* pCon,
                                  P2PaddrSTR  strThatP2Paddr,
                                  const void* pvLoginMsg,
                                  P2Psize_t   iSize) override
    { Trace(L"On_ConLogin", pCon);
      return P2PeerHub::On_ConLogin(pCon, strThatP2Paddr, pvLoginMsg, iSize); }

    virtual conRESULT On_ConClose(P2PeerCon* pCon) override
    { Trace(L"On_ConClose", pCon); return P2PeerHub::On_ConClose(pCon); }

    virtual conRESULT On_ConShutdown(P2PeerCon* pCon) override
    { Trace(L"On_ConShutdown", pCon); return P2PeerHub::On_ConShutdown(pCon); }

private:
    void Trace(LPCWSTR lpszStage, P2PeerCon* pCon)
    {
        LPCWSTR lpszAddr = L"<n/a>";
        try { if (pCon) lpszAddr = (P2PaddrSTR)pCon->GetP2Paddress(); }
        catch (...) { lpszAddr = L"<err>"; }

        SYSTEMTIME st; GetLocalTime(&st);
        wprintf(L"[%02d:%02d:%02d.%03d tid=%lu %s] %-16s con=%p addr='%s'\n",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                GetCurrentThreadId(),
                m_bServer ? L"SERVER" : L"CLIENT",
                lpszStage, (void*)pCon, lpszAddr);
        fflush(stdout);
    }

    void PrintMessage(LPCWSTR lpszKind, P2PeerMsg* pMsg)
    {
        LPCWSTR lpszSrc  = pMsg ? pMsg->GetSource() : L"<null>";
        LPCWSTR lpszData = (pMsg && pMsg->Data() && pMsg->DataSize() > 0)
                         ? (LPCWSTR)pMsg->Data()
                         : L"<no data>";

        wprintf(L"\n[%s] %s from '%s':\n  > %s\n\n",
                m_bServer ? L"SERVER" : L"CLIENT",
                lpszKind, lpszSrc, lpszData);
        fflush(stdout);

        if (m_bServer && g_hDoneEvent)
            SetEvent(g_hDoneEvent);
    }

    void PostTestMessage()
    {
        LPCWSTR   lpszMsg = L"Hello over a named pipe, in one process!";
        P2Psize_t nBytes  = (P2Psize_t)((wcslen(lpszMsg) + 1) * sizeof(wchar_t));

        P2PeerMsg32* pMsg = new P2PeerMsg32(
            kClientAddr, kServerAddr, P2Pmsg_BCast, lpszMsg, nBytes);

        PostP2PeerMsg(pMsg);
        wprintf(L"[CLIENT] Posted BCast: \"%s\"\n", lpszMsg);
        fflush(stdout);
    }

private:
    bool m_bServer;
    bool m_bSent;
};


// =========================================================================
// main
// =========================================================================
int main(int /*argc*/, char* /*argv*/[])
{
    _setmode(_fileno(stdout), _O_U16TEXT);

    // Route asserts to our hook (no modal dialog / no debugger break).
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportHook(AssertReportHook);

    wprintf(L"=== PipeMeshTest - single-process, two-hub NAMED PIPE probe ===\n");
    wprintf(L"Pipe : %s\n\n", kPipeName);
    fflush(stdout);

    g_hDoneEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (!StartupP2Pmsg(16))
    {
        wprintf(L"FATAL: StartupP2Pmsg() failed.\n");
        return 1;
    }
    // Named pipes don't need Winsock, but base classes may touch it; match
    // AlexTest and initialise it anyway (harmless).
    WSADATA oWsaData;
    WSAStartup(MAKEWORD(2, 2), &oWsaData);

    // ---- Hub A: SERVER (creates + listens on the named pipe) -------------
    PipeMeshHub oServer(kServerAddr, /*bServer*/ true);
    //  ARMING: RequireAuth defaults to ON since ProductionPlan.md Stage 3
    //  step 8, and a hub that requires authentication it cannot enforce
    //  REFUSES TO ARM - SpawnHub() returns NULL rather than starting and
    //  then turning every peer away. This example provisions no identity
    //  and no allow-list, so it takes the documented one-line migration
    //  and says so out loud. NOT the posture to copy into a real hub.
    oServer.RequireAuth ( false );
    HANDLE hServerThread = oServer.SpawnHub();
    if (!hServerThread) { wprintf(L"FATAL: server SpawnHub failed.\n"); return 1; }
    LogAt(L"SERVER", L"hub thread started");

    P2PeerConPipe* pSvcCon = P2PeerConPipe::ServiceFactory(kClientAddr, kPipeName);
    if (!pSvcCon) { wprintf(L"FATAL: ServiceFactory failed.\n"); return 1; }
    oServer.PostP2PeerCon(pSvcCon);
    LogAt(L"SERVER", L"service pipe connection posted (CreateNamedPipe + ConnectNamedPipe)");

    // Give the server pump a moment to actually create the pipe and post its
    // overlapped ConnectNamedPipe wait, so the client's CreateFile(OPEN_EXISTING)
    // has something to open.
    Sleep(750);

    // ---- Hub B: CLIENT (opens the existing named pipe) -------------------
    PipeMeshHub oClient(kClientAddr, /*bServer*/ false);
    oClient.RequireAuth ( false );          // as above - unprovisioned example
    HANDLE hClientThread = oClient.SpawnHub();
    if (!hClientThread) { wprintf(L"FATAL: client SpawnHub failed.\n"); return 1; }
    LogAt(L"CLIENT", L"hub thread started");

    P2PeerConPipe* pCliCon = P2PeerConPipe::ClientFactory(kServerAddr, kPipeName);
    if (!pCliCon) { wprintf(L"FATAL: ClientFactory failed.\n"); return 1; }
    oClient.PostP2PeerCon(pCliCon);
    LogAt(L"CLIENT", L"client pipe connection posted (CreateFile OPEN_EXISTING)");

    // ---- Wait for the round trip -----------------------------------------
    LogAt(L"MAIN", L"waiting up to 10s for login-ack + BCast delivery...");
    DWORD dwResult = WaitForSingleObject(g_hDoneEvent, 10000);

    int nExit;
    if (dwResult == WAIT_OBJECT_0)
    {
        LogAt(L"MAIN", L"SUCCESS - server received the client's BCast over the pipe");
        nExit = 0;
    }
    else
    {
        LogAt(L"MAIN", L"TIMEOUT - no BCast delivered (handshake did not complete)");
        nExit = 3;
    }

    // ---- Shutdown (servers/clients down, threads joined, kernel last) -----
    LogAt(L"MAIN", L"shutdown begin");
    oClient.CloseHub();
    oServer.CloseHub();
    WaitForSingleObject(hClientThread, 3000);
    WaitForSingleObject(hServerThread, 3000);
    CloseHandle(hClientThread);
    CloseHandle(hServerThread);

    CleanupP2Pmsg();
    if (g_hDoneEvent) { CloseHandle(g_hDoneEvent); g_hDoneEvent = NULL; }
    WSACleanup();

    wprintf(L"Done (exit=%d).\n", nExit);
    fflush(stdout);
    return nExit;
}

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
// WsaMeshTest.cpp
//
// SINGLE-PROCESS, TWO-HUB LOOPBACK-TCP connectivity probe for Targetcore.
//
// Question this harness answers:
//   Can two P2PeerHub's living in ONE process connect to each other over a
//   loopback TCP socket (P2PeerConWsa on 127.0.0.1) and complete the
//   Targetcore login handshake -- i.e. does the historical in-process
//   loopback-TCP failure (ASSERT(pCon==nullptr) at P2Pwin32.cpp:3844,
//   no On_ConLoginAck) still occur against the CURRENT (fixed) DLLs?
//
// It mirrors PipeMeshTest exactly, with ONE difference: the transport is
// P2PeerConWsa (loopback TCP) instead of P2PeerConPipe. Both the server hub
// and the client hub run in this one process, each on its own SpawnHub()
// pump thread.
//
// Verdict is reported by process EXIT CODE (so a headless run is unambiguous):
//   0 = SUCCESS   : client got On_ConLoginAck AND server received the BCast.
//   2 = ASSERT    : an MFC/CRT assertion fired (banner printed; a 3844 hit
//                   means the in-process loopback-TCP path still fails).
//   3 = TIMEOUT   : no assert, but handshake/delivery did not complete in time.
//   1 = SETUP     : startup/factory failure.

#include "stdafx.h"
#include "WsaMeshTest.h"

#include <io.h>
#include <fcntl.h>
#include <crtdbg.h>

#include "P2Pwin32.h"
#include "P2PeerHub.h"
#include "P2PeerConWsa.h"
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
static const short        kTestPort   = 7801;
static const P2PaddrSTR   kServerAddr = L"WsaMesh.Server";
static const P2PaddrSTR   kClientAddr = L"WsaMesh.Client";

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
// Assertion trap: turn a debug ASSERT into a deterministic exit(2) with a
// banner (including file:line) instead of a modal dialog / debugger break.
// -------------------------------------------------------------------------
static int __cdecl AssertReportHook(int nReportType, char* szMsg, int* pnRet)
{
    if (nReportType == _CRT_ASSERT)
    {
        fflush(stdout);
        fprintf(stderr,
                "\n=== ASSERT TRIPPED (single-process loopback-TCP mesh) ===\n%s\n"
                "=== interpreting as: in-process loopback-TCP connect FAILED ===\n",
                szMsg ? szMsg : "(no message)");
        fflush(stderr);
        if (pnRet) *pnRet = 0;   // don't invoke the debugger
        ExitProcess(2);
    }
    return FALSE;                // let other report types flow normally
}

// =========================================================================
// WsaMeshHub : instrumented P2PeerHub, identical shape to PipeMeshTest.
// =========================================================================
class WsaMeshHub : public P2PeerHub
{
public:
    WsaMeshHub(P2PaddrSTR strAddr, bool bServer)
        : P2PeerHub(strAddr)
        , m_bServer(bServer)
        , m_bSent(false)
    {}
    virtual ~WsaMeshHub() {}

protected:
    virtual msgRESULT On_P2PeerBCast(P2PeerMsg* pMsg) override
    { PrintMessage(L"BCast", pMsg); return msgHANDLED; }

    virtual msgRESULT On_P2PeerUCast(P2PeerMsg* pMsg) override
    { PrintMessage(L"UCast", pMsg); return msgHANDLED; }

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
            wprintf(L"[CLIENT] Login ack from '%s' - loopback TCP connection ready.\n",
                    strThatP2Paddr);
            PostTestMessage();
            m_bSent = true;
        }
        return result;
    }

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
        LPCWSTR   lpszMsg = L"Hello over loopback TCP, in one process!";
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

    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportHook(AssertReportHook);

    wprintf(L"=== WsaMeshTest - single-process, two-hub LOOPBACK-TCP probe ===\n");
    wprintf(L"Port : %d (127.0.0.1)\n\n", (int)kTestPort);
    fflush(stdout);

    g_hDoneEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (!StartupP2Pmsg(16))
    {
        wprintf(L"FATAL: StartupP2Pmsg() failed.\n");
        return 1;
    }
    WSADATA oWsaData;
    WSAStartup(MAKEWORD(2, 2), &oWsaData);

    // ---- Hub A: SERVER (listens on the loopback port) --------------------
    WsaMeshHub oServer(kServerAddr, /*bServer*/ true);
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

    // ServiceFactory's first arg = the EXPECTED remote peer address (the client).
    P2PeerConWsa* pSvcCon = P2PeerConWsa::ServiceFactory(kClientAddr, kTestPort);
    if (!pSvcCon) { wprintf(L"FATAL: ServiceFactory failed.\n"); return 1; }
    oServer.PostP2PeerCon(pSvcCon);
    LogAt(L"SERVER", L"service TCP connection posted (bind INADDR_ANY + listen)");

    // Give the server pump a moment to bind + listen before the client dials.
    Sleep(750);

    // ---- Hub B: CLIENT (dials 127.0.0.1:kTestPort) -----------------------
    WsaMeshHub oClient(kClientAddr, /*bServer*/ false);
    oClient.RequireAuth ( false );          // as above - unprovisioned example
    HANDLE hClientThread = oClient.SpawnHub();
    if (!hClientThread) { wprintf(L"FATAL: client SpawnHub failed.\n"); return 1; }
    LogAt(L"CLIENT", L"hub thread started");

    // ClientFactory: (server hub addr, ip, port).
    P2PeerConWsa* pCliCon = P2PeerConWsa::ClientFactory(kServerAddr, L"127.0.0.1", kTestPort);
    if (!pCliCon) { wprintf(L"FATAL: ClientFactory failed.\n"); return 1; }
    oClient.PostP2PeerCon(pCliCon);
    LogAt(L"CLIENT", L"client TCP connection posted (connect 127.0.0.1)");

    // ---- Wait for the round trip -----------------------------------------
    LogAt(L"MAIN", L"waiting up to 10s for login-ack + BCast delivery...");
    DWORD dwResult = WaitForSingleObject(g_hDoneEvent, 10000);

    int nExit;
    if (dwResult == WAIT_OBJECT_0)
    {
        LogAt(L"MAIN", L"SUCCESS - server received the client's BCast over loopback TCP");
        nExit = 0;
    }
    else
    {
        LogAt(L"MAIN", L"TIMEOUT - no BCast delivered (handshake did not complete)");
        nExit = 3;
    }

    // ---- Shutdown --------------------------------------------------------
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

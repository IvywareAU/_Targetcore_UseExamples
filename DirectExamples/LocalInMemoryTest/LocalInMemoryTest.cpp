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
// LocalInMemoryTest.cpp
//
// SINGLE-PROCESS, TWO-HUB *in-memory* delivery example for Targetcore.
//
// What this demonstrates
// ----------------------
// Two P2PeerHub's live in ONE process, each on its own SpawnHub() pump
// thread, and exchange P2PeerMsg's WITHOUT any connection between them --
// no socket, no named pipe, no login handshake, no P2PeerCon at all.
//
// The mechanism is the exported pump-injection call:
//
//     PostP2Pmsg( pMsg, targetHub.GetHubID() );
//
// Each hub's SpawnHub() pump drains its own P2Pmsg queue in the same loop
// as its IOCP. PostP2Pmsg() drops a message straight onto a named hub's
// pump queue; that hub's pump then dispatches it. Because the message's
// destination address equals the target hub's own address, the pump fires
// On_P2PeerBCast on that hub -- exactly as if it had arrived over a wire.
//
// This is the lightest possible way to realise a multi-hub mesh in one
// address space: it skips the whole transport/handshake layer that
// P2PeerConWsa / P2PeerConPipe exercise. Use it when every hub is in-proc
// and you only need routing/dispatch semantics, not a real network.
//
// Threading rule (important)
// --------------------------
// PostP2Pmsg( msg, otherHubId ) is only legal from a NON-hub thread (here,
// main). Calling it from INSIDE one hub's pump thread to target a DIFFERENT
// hub trips a cross-hub-context ASSERT in the kernel. So all sends in this
// example originate on the main thread.
//
// Verdict is reported by process EXIT CODE (unambiguous for a headless run):
//   0 = SUCCESS : both hubs received the BCast addressed to them.
//   2 = ASSERT  : an MFC/CRT assertion fired (banner printed).
//   3 = TIMEOUT : no assert, but delivery did not complete in time.
//   1 = SETUP   : startup failure.

#include "stdafx.h"
#include "LocalInMemoryTest.h"

#include <io.h>
#include <fcntl.h>
#include <crtdbg.h>

#include "P2Pwin32.h"
#include "P2PeerHub.h"
#include "P2PeerMsg.h"
#include "Msgexception.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// MFC requires exactly one CWinApp instance per executable.
CWinApp theApp;

// -------------------------------------------------------------------------
// Mesh configuration -- two hubs, addressed by name. No transport endpoints.
// -------------------------------------------------------------------------
static const P2PaddrSTR kAddrHubA = L"LocalMesh.HubA";
static const P2PaddrSTR kAddrHubB = L"LocalMesh.HubB";

// Signalled when the corresponding hub receives the BCast addressed to it.
static HANDLE g_hRecvA = NULL;
static HANDLE g_hRecvB = NULL;

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
// Assertion trap: turn a debug ASSERT (which would pop a modal dialog and
// hang a headless run) into a deterministic exit code 2 with a banner.
// -------------------------------------------------------------------------
static int __cdecl AssertReportHook(int nReportType, char* szMsg, int* pnRet)
{
    if (nReportType == _CRT_ASSERT)
    {
        fflush(stdout);
        fprintf(stderr,
                "\n=== ASSERT TRIPPED (in-memory two-hub delivery) ===\n%s\n",
                szMsg ? szMsg : "(no message)");
        fflush(stderr);
        if (pnRet) *pnRet = 0;   // don't invoke the debugger
        ExitProcess(2);
    }
    return FALSE;                // let other report types flow normally
}

// =========================================================================
// LocalHub
//   A P2PeerHub that just traces its BCast/UCast arrivals and signals a
//   caller-supplied event when a BCast lands. No connection callbacks are
//   needed here -- there are no connections.
// =========================================================================
class LocalHub : public P2PeerHub
{
public:
    LocalHub(P2PaddrSTR strAddr, LPCWSTR lpszName, HANDLE hRecvEvent)
        : P2PeerHub(strAddr)
        , m_sName(lpszName)
        , m_hRecvEvent(hRecvEvent)
    {}
    virtual ~LocalHub() {}

protected:
    virtual msgRESULT On_P2PeerBCast(P2PeerMsg* pMsg) override
    {
        PrintMessage(L"BCast", pMsg);
        if (m_hRecvEvent) SetEvent(m_hRecvEvent);
        return msgHANDLED;
    }

    virtual msgRESULT On_P2PeerUCast(P2PeerMsg* pMsg) override
    {
        PrintMessage(L"UCast", pMsg);
        if (m_hRecvEvent) SetEvent(m_hRecvEvent);
        return msgHANDLED;
    }

private:
    void PrintMessage(LPCWSTR lpszKind, P2PeerMsg* pMsg)
    {
        LPCWSTR lpszSrc  = pMsg ? pMsg->GetSource() : L"<null>";
        LPCWSTR lpszData = (pMsg && pMsg->Data() && pMsg->DataSize() > 0)
                         ? (LPCWSTR)pMsg->Data()
                         : L"<no data>";

        wprintf(L"\n[%s] %s from '%s':\n  > %s\n\n",
                (LPCWSTR)m_sName, lpszKind, lpszSrc, lpszData);
        fflush(stdout);
    }

private:
    CString m_sName;
    HANDLE  m_hRecvEvent;   // not owned
};

// -------------------------------------------------------------------------
// Post a BCast from main (a non-hub thread) straight onto a target hub's
// pump queue. dst == the target hub's own address, so its pump dispatches
// it as On_P2PeerBCast.
// -------------------------------------------------------------------------
static void PostInMemoryBCast(P2PeerHub&  oTargetcore,
                              P2PaddrSTR  strSrc,
                              P2PaddrSTR  strDst,
                              LPCWSTR     lpszText)
{
    P2Psize_t    nBytes = (P2Psize_t)((wcslen(lpszText) + 1) * sizeof(wchar_t));
    P2PeerMsg32* pMsg   = new P2PeerMsg32(strSrc, strDst, P2Pmsg_BCast,
                                          lpszText, nBytes);

    // Deliver into the target hub's pump. GetHubID() returns the pump id.
    PostP2Pmsg(pMsg, oTargetcore.GetHubID());

    wprintf(L"[MAIN] PostP2Pmsg -> '%s' : \"%s\"\n", strDst, lpszText);
    fflush(stdout);
}

// =========================================================================
// main
// =========================================================================
int main(int /*argc*/, char* /*argv*/[])
{
    _setmode(_fileno(stdout), _O_U16TEXT);

    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportHook(AssertReportHook);

    wprintf(L"=== LocalInMemoryTest - two hubs, one process, no wire ===\n");
    wprintf(L"Delivery mechanism: PostP2Pmsg(msg, targetHub.GetHubID())\n\n");
    fflush(stdout);

    g_hRecvA = CreateEvent(NULL, FALSE, FALSE, NULL);
    g_hRecvB = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (!StartupP2Pmsg(16))
    {
        wprintf(L"FATAL: StartupP2Pmsg() failed.\n");
        return 1;
    }
    // Pure in-memory delivery needs no sockets, but P2PeerHub base classes
    // may touch Winsock; initialise it anyway (harmless), matching the
    // networked harnesses.
    WSADATA oWsaData;
    WSAStartup(MAKEWORD(2, 2), &oWsaData);

    // ---- Two hubs, each on its own pump thread. No connection between them.
    LocalHub oHubA(kAddrHubA, L"HubA", g_hRecvA);
    LocalHub oHubB(kAddrHubB, L"HubB", g_hRecvB);

    //  ARMING: RequireAuth defaults to ON since ProductionPlan.md Stage 3
    //  step 8, and a hub that requires authentication it cannot enforce
    //  REFUSES TO ARM - SpawnHub() returns NULL rather than starting and
    //  then turning every peer away. This example provisions no identity
    //  and no allow-list, so it takes the documented one-line migration
    //  and says so out loud. NOT the posture to copy into a real hub.
    oHubA.RequireAuth ( false );
    HANDLE hThreadA = oHubA.SpawnHub();
    oHubB.RequireAuth ( false );          // as above - unprovisioned example
    HANDLE hThreadB = oHubB.SpawnHub();
    if (!hThreadA || !hThreadB)
    {
        wprintf(L"FATAL: SpawnHub failed.\n");
        return 1;
    }
    LogAt(L"MAIN", L"both hub pump threads started");

    // ---- Send in both directions, entirely in memory. -------------------
    // A -> B
    PostInMemoryBCast(oHubB, kAddrHubA, kAddrHubB,
                      L"Hello HubB - delivered in memory, no wire!");
    // B -> A
    PostInMemoryBCast(oHubA, kAddrHubB, kAddrHubA,
                      L"Hello HubA - same process, straight to your pump!");

    // ---- Wait for both hubs to dispatch their BCast. --------------------
    LogAt(L"MAIN", L"waiting up to 5s for both in-memory deliveries...");
    HANDLE  hBoth[2] = { g_hRecvA, g_hRecvB };
    DWORD   dwResult = WaitForMultipleObjects(2, hBoth, /*bWaitAll*/ TRUE, 5000);

    int nExit;
    if (dwResult == WAIT_OBJECT_0)
    {
        LogAt(L"MAIN", L"SUCCESS - both hubs received their in-memory BCast");
        nExit = 0;
    }
    else
    {
        LogAt(L"MAIN", L"TIMEOUT - one or both deliveries did not complete");
        nExit = 3;
    }

    // ---- Shutdown: hubs down, threads joined, kernel last. ---------------
    LogAt(L"MAIN", L"shutdown begin");
    oHubA.CloseHub();
    oHubB.CloseHub();
    WaitForSingleObject(hThreadA, 3000);
    WaitForSingleObject(hThreadB, 3000);
    CloseHandle(hThreadA);
    CloseHandle(hThreadB);

    CleanupP2Pmsg();
    if (g_hRecvA) { CloseHandle(g_hRecvA); g_hRecvA = NULL; }
    if (g_hRecvB) { CloseHandle(g_hRecvB); g_hRecvB = NULL; }
    WSACleanup();

    wprintf(L"Done (exit=%d).\n", nExit);
    fflush(stdout);
    return nExit;
}

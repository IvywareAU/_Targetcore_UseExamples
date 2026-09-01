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
// TwoConTest.cpp
//
// TEST 1 -- Can a SINGLE P2PeerHub supervise TWO P2PeerConWsa connections?
//
// Question:
//   Create one P2PeerHub. Create two WSA connections in the same process:
//       ConServer = P2PeerConWsa::ServiceFactory(...)   // passive, listens
//       ConClient = P2PeerConWsa::ClientFactory(...)     // active, connects
//   Hand BOTH to the SAME hub:
//       hub.PostP2PeerCon(ConServer);
//       hub.PostP2PeerCon(ConClient);
//   Is that allowed? Does the hub drive both, letting them connect to each
//   other over TCP loopback -- i.e. the hub logging in to itself?
//
// What the TargetCore source says (verified, not guessed):
//   * A P2PeerHub owns a LIST of P2PeerCon's (EnumP2PmsgCon) -- it is built to
//     supervise many connections, so two is fine IN PRINCIPLE.
//   * BUT P2PeerHub::PostP2PeerCon (P2PeerHub.cpp:432-447) REJECTS a second
//     connection whose identification address (P2PeerCon::GetP2Paddress(), i.e.
//     m_oThatP2Paddr -- the REMOTE peer address) duplicates one already posted:
//         "P2PeerCon[%s] instance already exists within P2PmsgHub[%s]" -> FALSE
//   => Two connections coexist under one hub IFF they carry DISTINCT peer
//      addresses. Give both the same "that" address and the 2nd post fails.
//
// This harness proves both facts at runtime:
//   PART A (positive): two cons with DISTINCT peer addresses -> both posts
//                      return TRUE, the hub drives both lifecycles, and (over
//                      loopback) the client con completes On_ConLoginAck.
//   PART B (negative): posting a 3rd con whose peer address DUPLICATES an
//                      existing one -> PostP2PeerCon returns FALSE.
//
// Verdict via EXIT CODE (unambiguous for a headless run):
//   0 = PASS : both distinct-address posts returned TRUE (one hub, two cons)
//              AND the duplicate-address post was rejected (returned FALSE).
//   2 = an MFC/CRT assertion fired (banner printed).
//   3 = a post that should have succeeded returned FALSE, or the duplicate
//       post was NOT rejected -- i.e. behaviour differs from the source.
//   1 = setup failure (startup / factory / SpawnHub).

#include "stdafx.h"
#include "TwoConTest.h"

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
// Configuration
// -------------------------------------------------------------------------
static const short      kPort    = 7788;

// The ONE hub's own address.
static const P2PaddrSTR kHubAddr = L"TwoConTest.Hub";

// Distinct REMOTE-peer addresses for the two connections. They MUST differ,
// otherwise PostP2PeerCon dedups the second one away (that is PART B).
static const P2PaddrSTR kSvcPeer = L"TwoConTest.PeerA";   // ConServer's "that"
static const P2PaddrSTR kCliPeer = L"TwoConTest.PeerB";   // ConClient's "that"

// Signalled when the client-side connection completes its login handshake.
static HANDLE g_hLoginAck = NULL;

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
// Assertion trap: turn a modal debug ASSERT into a deterministic exit(2)
// with a printed banner (so a headless run cannot hang).
// -------------------------------------------------------------------------
static int __cdecl AssertReportHook(int nReportType, char* szMsg, int* pnRet)
{
    if (nReportType == _CRT_ASSERT)
    {
        fflush(stdout);
        fprintf(stderr, "\n=== ASSERT TRIPPED ===\n%s\n",
                szMsg ? szMsg : "(no message)");
        fflush(stderr);
        if (pnRet) *pnRet = 0;   // don't invoke the debugger
        ExitProcess(2);
    }
    return FALSE;
}

// =========================================================================
// TwoConHub
//   A single hub that owns BOTH connections. Because it is both the "server"
//   (accepts) and the "client" (connects) it will, over loopback, log in to
//   itself. Every connection-lifecycle stage is traced with the con pointer,
//   so the console shows the hub concurrently driving two distinct P2PeerCon
//   objects (plus the accept-spawned one).
// =========================================================================
class TwoConHub : public P2PeerHub
{
public:
    TwoConHub(P2PaddrSTR strAddr) : P2PeerHub(strAddr) {}
    virtual ~TwoConHub() {}

protected:
    // ---- Application message handlers ----------------------------------
    virtual msgRESULT On_P2PeerBCast(P2PeerMsg* pMsg) override
    { Print(L"BCast", pMsg); return msgHANDLED; }

    virtual msgRESULT On_P2PeerUCast(P2PeerMsg* pMsg) override
    { Print(L"UCast", pMsg); return msgHANDLED; }

    // ---- Connection lifecycle (inherited from P2PeerTarget) ------------
    // Client side finished the handshake: the hub connected to itself.
    virtual conRESULT On_ConLoginAck(P2PeerCon*  pCon,
                                     P2PaddrSTR  strThisAddr,
                                     P2PaddrSTR  strThatAddr,
                                     const void* pvAck,
                                     P2Psize_t   iSize) override
    {
        Trace(L"On_ConLoginAck", pCon);
        conRESULT r = P2PeerHub::On_ConLoginAck(
                          pCon, strThisAddr, strThatAddr, pvAck, iSize);
        if (g_hLoginAck) SetEvent(g_hLoginAck);
        return r;
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
                                  P2PaddrSTR  strThatAddr,
                                  const void* pvLoginMsg,
                                  P2Psize_t   iSize) override
    { Trace(L"On_ConLogin", pCon);
      return P2PeerHub::On_ConLogin(pCon, strThatAddr, pvLoginMsg, iSize); }

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
        wprintf(L"[%02d:%02d:%02d.%03d tid=%lu HUB] %-16s con=%p addr='%s'\n",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                GetCurrentThreadId(), lpszStage, (void*)pCon, lpszAddr);
        fflush(stdout);
    }

    void Print(LPCWSTR kind, P2PeerMsg* pMsg)
    {
        LPCWSTR src  = pMsg ? pMsg->GetSource() : L"<null>";
        LPCWSTR data = (pMsg && pMsg->Data() && pMsg->DataSize() > 0)
                     ? (LPCWSTR)pMsg->Data() : L"<no data>";
        wprintf(L"\n[HUB] %s from '%s': %s\n\n", kind, src, data);
        fflush(stdout);
    }
};


// =========================================================================
// main
// =========================================================================
int main()
{
    _setmode(_fileno(stdout), _O_U16TEXT);

    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportHook(AssertReportHook);

    wprintf(L"=== TwoConTest -- one P2PeerHub, two P2PeerConWsa connections ===\n");
    wprintf(L"Hub  : %s\n", kHubAddr);
    wprintf(L"Port : %d (TCP loopback 127.0.0.1)\n\n", (int)kPort);
    fflush(stdout);

    g_hLoginAck = CreateEvent(NULL, FALSE, FALSE, NULL);

    // ---- Kernel + Winsock ---------------------------------------------
    if (!StartupP2Pmsg(16))
    { wprintf(L"FATAL: StartupP2Pmsg() failed.\n"); return 1; }

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    { wprintf(L"FATAL: WSAStartup() failed.\n"); return 1; }

    // ---- The SINGLE hub -----------------------------------------------
    TwoConHub oHub(kHubAddr);
    //  ARMING: RequireAuth defaults to ON since ProductionPlan.md Stage 3
    //  step 8, and a hub that requires authentication it cannot enforce
    //  REFUSES TO ARM - SpawnHub() returns NULL rather than starting and
    //  then turning every peer away. This example provisions no identity
    //  and no allow-list, so it takes the documented one-line migration
    //  and says so out loud. NOT the posture to copy into a real hub.
    oHub.RequireAuth ( false );
    HANDLE hThread = oHub.SpawnHub();
    if (!hThread)
    { wprintf(L"FATAL: SpawnHub() failed.\n"); return 1; }
    LogAt(L"MAIN", L"single hub spawned");

    int nExit = 0;

    // ===================================================================
    // PART A -- two connections, DISTINCT peer addresses, one hub
    // ===================================================================
    LogAt(L"MAIN", L"PART A: posting Service + Client con to the SAME hub");

    // (1) The listening (service) connection. "that" = kSvcPeer.
    P2PeerConWsa* pConServer =
        P2PeerConWsa::ServiceFactory(kSvcPeer, kPort);
    if (!pConServer)
    { wprintf(L"FATAL: ServiceFactory failed.\n"); return 1; }

    BOOL bServerPosted = oHub.PostP2PeerCon(pConServer);
    wprintf(L"  PostP2PeerCon(ConServer, that='%s') -> %s\n",
            kSvcPeer, bServerPosted ? L"TRUE (accepted)" : L"FALSE (rejected)");
    fflush(stdout);

    // Give the hub a moment to arm the listen/accept before the client dials.
    Sleep(750);

    // (2) The connecting (client) connection. "that" = kCliPeer (distinct).
    P2PeerConWsa* pConClient =
        P2PeerConWsa::ClientFactory(kCliPeer, L"127.0.0.1", kPort);
    if (!pConClient)
    { wprintf(L"FATAL: ClientFactory failed.\n"); return 1; }

    BOOL bClientPosted = oHub.PostP2PeerCon(pConClient);
    wprintf(L"  PostP2PeerCon(ConClient, that='%s') -> %s\n",
            kCliPeer, bClientPosted ? L"TRUE (accepted)" : L"FALSE (rejected)");
    fflush(stdout);

    if (!bServerPosted || !bClientPosted)
    {
        LogAt(L"MAIN", L"PART A FAILED: a distinct-address post was rejected");
        nExit = 3;
    }
    else
    {
        LogAt(L"MAIN", L"PART A: both posts accepted -- one hub now owns 2 cons");
        // Optional: watch the loopback handshake complete (hub logs in to
        // itself). Not required for the verdict, but proves the two cons are
        // live and talking. 10s ceiling.
        DWORD dw = WaitForSingleObject(g_hLoginAck, 10000);
        LogAt(L"MAIN", dw == WAIT_OBJECT_0
                       ? L"PART A: loopback On_ConLoginAck fired (hub<->hub)"
                       : L"PART A: no LoginAck in 10s (posts still succeeded)");
    }

    // ===================================================================
    // PART B -- duplicate peer address must be REJECTED
    // ===================================================================
    LogAt(L"MAIN", L"PART B: posting a 3rd con that DUPLICATES ConServer's addr");

    // Same "that" address (kSvcPeer) as pConServer -> must be refused.
    // Wrap it in a SafeP2PeerCon so the never-posted con is destroyed
    // correctly on scope exit (a fresh factory con has m_cRef==0; the
    // Safe wrapper AddRef()s to 1 then Release()s back to 0, which runs the
    // proper Destroy/delete path -- calling Release() directly would
    // underflow m_cRef and trip an assert).
    {
        P2PeerConWsa*  pConDup =
            P2PeerConWsa::ServiceFactory(kSvcPeer, (short)(kPort + 1));
        SafeP2PeerCon  oSafeDup = pConDup;   // owns it for this scope

        BOOL bDupPosted = pConDup ? oHub.PostP2PeerCon(pConDup) : FALSE;
        wprintf(L"  PostP2PeerCon(ConDup, that='%s') -> %s\n",
                kSvcPeer,
                bDupPosted ? L"TRUE (UNEXPECTED)"
                           : L"FALSE (correctly rejected)");
        fflush(stdout);

        if (bDupPosted)
        {
            // Hub accepted a duplicate address -- contradicts the source.
            LogAt(L"MAIN", L"PART B FAILED: duplicate address was NOT rejected");
            nExit = 3;
        }
        else
        {
            LogAt(L"MAIN", L"PART B: duplicate correctly rejected");
        }
    }   // oSafeDup destructs here -> Release -> con destroyed

    // ===================================================================
    // Verdict
    // ===================================================================
    if (nExit == 0)
        LogAt(L"MAIN", L"VERDICT: PASS -- one hub supervises two connections "
                       L"(distinct addrs); duplicate addr is rejected");
    else
        LogAt(L"MAIN", L"VERDICT: FAIL -- see PART A/B lines above");

    // ---- Shutdown ------------------------------------------------------
    LogAt(L"MAIN", L"shutdown begin");
    oHub.CloseHub();
    WaitForSingleObject(hThread, 3000);
    CloseHandle(hThread);

    CleanupP2Pmsg();
    if (g_hLoginAck) { CloseHandle(g_hLoginAck); g_hLoginAck = NULL; }
    WSACleanup();

    wprintf(L"Done (exit=%d).\n", nExit);
    fflush(stdout);
    return nExit;
}

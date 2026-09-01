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
// MixConTest.cpp
//
// TEST 2 -- Can ONE P2PeerHub hold a MIX of transports at once:
//           a P2PeerConWsa (TCP) connection AND a P2PeerConPipe (named pipe)
//           connection, both live simultaneously on the same hub?
//
// ---------------------------------------------------------------------------
// Why this is NOT just "test 1 with a pipe added"
// ---------------------------------------------------------------------------
// A hub identifies each connection by the REMOTE hub's address. During login a
// connection's m_oThatP2Paddr is rewritten to the peer hub's real address, and
// P2PeerHub keeps at most ONE connection per remote identity (it is the same
// "distinct address" rule test 1 hit at post time -- here it bites again after
// login). Transport type is never part of the key.
//
// Consequence: a single hub that loops back to ITSELF on two transports makes
// BOTH server-accept connections resolve to the same identity ("...Hub") and
// the 2nd is rejected as a duplicate ("Duplicate P2PeerCon's for P2PeerHub").
// So a self-loop cannot show two live mixed connections -- not because of the
// mix, but because both ends collapse to one identity.
//
// To genuinely put a WSA con AND a pipe con on ONE hub at the same time, the
// two connections must target DIFFERENT remote hubs. Three hubs, one process:
//
//        HubB (WSA service) <=== TCP  127.0.0.1:7799 ===  HubA  (WSA client)
//        HubC (Pipe service) <== \\.\pipe\MixConProbe ==   |    (Pipe client)
//                                                          `--- THE MIXED HUB
//
//   * HubA is the star: it simultaneously owns
//        - a P2PeerConWsa  whose peer identity is "HubB"
//        - a P2PeerConPipe whose peer identity is "HubC"
//     -> distinct identities, no collision, BOTH handshakes complete.
//   * HubA is the client (connector) on both, so HubA receives On_ConLoginAck
//     twice -- once per transport. That double-ack IS the proof of connectivity.
//
// ---------------------------------------------------------------------------
// Message round-trip: PostP2PeerMsg with a response over BOTH transports
// ---------------------------------------------------------------------------
// Login only proves the transports are up. To prove the mixed hub can actually
// carry APPLICATION traffic (request + response) on each transport, once a
// transport's login is acked HubA posts a P2PeerMsg (P2Pmsg_BCast) addressed to
// that transport's peer:
//
//     HubA --request--> HubB (over the WSA socket) --response--> HubA
//     HubA --request--> HubC (over the named pipe) --response--> HubA
//
// Every hub overrides On_P2PeerBCast to LOG each received message. HubB and HubC
// recognise a request from HubA and post a response straight back (dest = HubA,
// so it routes over the very connection it arrived on). HubA logs both responses
// -- one that came back over the socket, one over the pipe. Both responses
// arriving is the proof that PostP2PeerMsg works with a reply on each transport.
//
// Verdict via EXIT CODE:
//   0 = PASS : HubA completed On_ConLoginAck on BOTH the WSA and the pipe
//              connection AND received a response over BOTH transports.
//   2 = an MFC/CRT assertion fired.
//   3 = a post that should have succeeded returned FALSE, a transport did not
//       complete login within the timeout, or a response did not come back.
//   1 = setup failure (startup / factory / SpawnHub).

#include "stdafx.h"
#include "MixConTest.h"

#include <crtdbg.h>

#include "P2Pwin32.h"
#include "P2PeerHub.h"
#include "P2PeerConWsa.h"
#include "P2PeerConPipe.h"
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
static const short      kPort     = 7799;
static LPCTSTR          kPipeName = _T("\\\\.\\pipe\\MixConProbe");

static const P2PaddrSTR kHubA = L"MixConTest.HubA";   // the MIXED hub
static const P2PaddrSTR kHubB = L"MixConTest.HubB";   // WSA  peer of HubA
static const P2PaddrSTR kHubC = L"MixConTest.HubC";   // Pipe peer of HubA

// LoginAck observation on HubA, one flag per transport (keyed by the con's
// peer identity: a HubA con to HubB is the WSA one; to HubC is the pipe one).
static LONG   g_nWsaAck  = 0;
static LONG   g_nPipeAck = 0;
static HANDLE g_hBothAck = NULL;

// Response observation on HubA, one flag per transport (a response from HubB
// arrived over the socket; a response from HubC arrived over the pipe).
static LONG   g_nWsaRsp  = 0;
static LONG   g_nPipeRsp = 0;
static HANDLE g_hBothRsp = NULL;

// The P2PeerMsg name used for both the request and its response. It must match
// the hub's P2PeerMsg_MAP entry for On_P2PeerBCast, so we reuse P2Pmsg_BCast
// (a message named "P2PmsgBCast") -- the same id WsaMeshTest/PipeMeshTest use.
// Request vs response is told apart purely by the source address.

// -------------------------------------------------------------------------
// NOTE on console mode: stdout is left in its default translated mode (NOT
// _O_U16TEXT). With the pipe transport active, wide (wprintf) and narrow
// stdio writes can mix on stdout; a stream in _O_U16TEXT asserts inside the
// UCRT the instant a narrow write reaches it. All text here is ASCII, so the
// default mode prints everything correctly and avoids that assert.
// -------------------------------------------------------------------------
static void LogAt(LPCWSTR lpszRole, LPCWSTR lpszMsg)
{
    SYSTEMTIME st; GetLocalTime(&st);
    wprintf(L"[%02d:%02d:%02d.%03d tid=%lu %s] %s\n",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            GetCurrentThreadId(), lpszRole, lpszMsg);
    fflush(stdout);
}

static int __cdecl AssertReportHook(int nReportType, char* szMsg, int* pnRet)
{
    if (nReportType == _CRT_ASSERT)
    {
        fflush(stdout);
        fprintf(stderr, "\n=== ASSERT TRIPPED ===\n%s\n",
                szMsg ? szMsg : "(no message)");
        fflush(stderr);
        if (pnRet) *pnRet = 0;
        ExitProcess(2);
    }
    return FALSE;
}

// =========================================================================
// MixConHub
//   One instrumented hub subclass, reused for all three hubs. Only HubA ever
//   fires On_ConLoginAck (it is the sole connector), and only for peers HubB
//   (its WSA con) and HubC (its pipe con) -- exactly the two mixed transports.
// =========================================================================
class MixConHub : public P2PeerHub
{
public:
    MixConHub(P2PaddrSTR strAddr, LPCWSTR lpszLabel)
        : P2PeerHub(strAddr), m_strAddr(strAddr), m_strLabel(lpszLabel) {}
    virtual ~MixConHub() {}

protected:
    // ----- Message routing: log every received P2PeerMsg, and let the two
    //       service hubs reply to a request so HubA sees a response come back
    //       over each transport. P2Pmsg_BCast routes here via the hub's
    //       P2PeerMsg_MAP (see P2PeerHub.cpp: ON_P2PeerMsg(P2Pmsg_BCast,...)).
    virtual msgRESULT On_P2PeerBCast(P2PeerMsg* pMsg) override
    {
        LPCWSTR lpszSrc  = pMsg ? pMsg->GetSource() : L"<null>";
        LPCWSTR lpszData = (pMsg && pMsg->Data() && pMsg->DataSize() > 0)
                         ? (LPCWSTR)pMsg->Data()
                         : L"<no data>";

        wprintf(L"[%-5s] RECV msg from '%s': \"%s\"\n",
                m_strLabel, lpszSrc, lpszData);
        fflush(stdout);

        // A request originates at HubA. HubB (WSA) and HubC (pipe) answer it;
        // the reply is addressed back to HubA so it routes over the same con.
        if (lpszSrc && wcscmp(lpszSrc, kHubA) == 0)
        {
            PostResponse(lpszData);
        }
        // A response arrives back at HubA from HubB (socket) or HubC (pipe).
        else if (lpszSrc && wcscmp(lpszSrc, kHubB) == 0)
        {
            InterlockedExchange(&g_nWsaRsp, 1);
            LogAt(m_strLabel, L"  <- response received over WSA (TCP)");
        }
        else if (lpszSrc && wcscmp(lpszSrc, kHubC) == 0)
        {
            InterlockedExchange(&g_nPipeRsp, 1);
            LogAt(m_strLabel, L"  <- response received over PIPE");
        }

        if (g_nWsaRsp && g_nPipeRsp && g_hBothRsp)
            SetEvent(g_hBothRsp);   // a reply came back on BOTH transports

        return msgHANDLED;
    }

    virtual conRESULT On_ConLoginAck(P2PeerCon*  pCon,
                                     P2PaddrSTR  strThisAddr,
                                     P2PaddrSTR  strThatAddr,
                                     const void* pvAck,
                                     P2Psize_t   iSize) override
    {
        Trace(L"On_ConLoginAck", pCon);
        conRESULT r = P2PeerHub::On_ConLoginAck(
                          pCon, strThisAddr, strThatAddr, pvAck, iSize);

        LPCWSTR addr = L"";
        try { addr = (P2PaddrSTR)pCon->GetP2Paddress(); } catch (...) {}

        if (addr && wcscmp(addr, kHubB) == 0)          // HubA's WSA con
        {
            InterlockedExchange(&g_nWsaAck, 1);
            LogAt(m_strLabel, L"  -> WSA (TCP) connection login acked");
            // Transport is live: send an application request over the socket.
            PostRequest(kHubB, L"Hello HubB over the WSA socket");
        }
        else if (addr && wcscmp(addr, kHubC) == 0)     // HubA's pipe con
        {
            InterlockedExchange(&g_nPipeAck, 1);
            LogAt(m_strLabel, L"  -> PIPE connection login acked");
            // Transport is live: send an application request over the pipe.
            PostRequest(kHubC, L"Hello HubC over the named pipe");
        }

        if (g_nWsaAck && g_nPipeAck && g_hBothAck)
            SetEvent(g_hBothAck);   // both transports live on the one hub
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

private:
    // HubA -> peer request. dest = the peer hub address, so RouteP2PeerMsg
    // sends it over the connection whose identity matches that peer.
    void PostRequest(P2PaddrSTR strToPeer, LPCWSTR lpszText)
    {
        P2Psize_t nBytes = (P2Psize_t)((wcslen(lpszText) + 1) * sizeof(wchar_t));
        P2PeerMsg32* pReq = new P2PeerMsg32(
            m_strAddr, strToPeer, P2Pmsg_BCast, lpszText, nBytes);
        PostP2PeerMsg(pReq);

        wprintf(L"[%-5s] SEND request to '%s': \"%s\"\n",
                m_strLabel, strToPeer, lpszText);
        fflush(stdout);
    }

    // Service hub (HubB/HubC) -> HubA response. dest = HubA, so it routes back
    // over the same connection the request arrived on.
    void PostResponse(LPCWSTR lpszRequestText)
    {
        wchar_t szReply[256];
        swprintf_s(szReply, L"ACK from %s: got \"%s\"", m_strLabel, lpszRequestText);
        P2Psize_t nBytes = (P2Psize_t)((wcslen(szReply) + 1) * sizeof(wchar_t));

        P2PeerMsg32* pRsp = new P2PeerMsg32(
            m_strAddr, kHubA, P2Pmsg_BCast, szReply, nBytes);
        PostP2PeerMsg(pRsp);

        wprintf(L"[%-5s] SEND response to '%s': \"%s\"\n",
                m_strLabel, kHubA, szReply);
        fflush(stdout);
    }

    void Trace(LPCWSTR lpszStage, P2PeerCon* pCon)
    {
        LPCWSTR lpszAddr = L"<n/a>";
        try { if (pCon) lpszAddr = (P2PaddrSTR)pCon->GetP2Paddress(); }
        catch (...) { lpszAddr = L"<err>"; }

        SYSTEMTIME st; GetLocalTime(&st);
        wprintf(L"[%02d:%02d:%02d.%03d tid=%lu %-5s] %-16s con=%p addr='%s'\n",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                GetCurrentThreadId(), m_strLabel,
                lpszStage, (void*)pCon, lpszAddr);
        fflush(stdout);
    }

    P2PaddrSTR m_strAddr;    // this hub's own address (message source)
    LPCWSTR    m_strLabel;
};


// =========================================================================
// main
// =========================================================================
int main()
{
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportHook(AssertReportHook);

    wprintf(L"=== MixConTest -- one hub holding P2PeerConWsa + P2PeerConPipe ===\n");
    wprintf(L"HubA (mixed) : %s\n", kHubA);
    wprintf(L"  WSA  -> HubB %s over TCP 127.0.0.1:%d\n", kHubB, (int)kPort);
    wprintf(L"  Pipe -> HubC %s over %s\n\n", kHubC, kPipeName);
    fflush(stdout);

    g_hBothAck = CreateEvent(NULL, TRUE, FALSE, NULL);   // manual-reset (login)
    g_hBothRsp = CreateEvent(NULL, TRUE, FALSE, NULL);   // manual-reset (response)

    if (!StartupP2Pmsg(16))
    { wprintf(L"FATAL: StartupP2Pmsg() failed.\n"); return 1; }

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    { wprintf(L"FATAL: WSAStartup() failed.\n"); return 1; }

    // ---- Three hubs, three pump threads --------------------------------
    MixConHub oHubA(kHubA, L"HubA");   // mixed endpoint (WSA client + pipe client)
    MixConHub oHubB(kHubB, L"HubB");   // WSA  service (accepts HubA)
    MixConHub oHubC(kHubC, L"HubC");   // Pipe service (accepts HubA)

    // Peer login auth is REQUIRED by default (Stage 3 step 8), and an
    // unprovisioned hub refuses to arm: SpawnHub() returns 0 before the pump
    // thread exists. This binary probes TRANSPORTS, not who may log in, so it
    // takes the documented one-line migration on each hub -- the same thing
    // MscsUnitTests/mix_con.cpp does. Must be called BEFORE SpawnHub().
    oHubA.RequireAuth(false);
    oHubB.RequireAuth(false);
    oHubC.RequireAuth(false);

    // Body sealing is likewise REQUIRED by default (Stage 3 step 20). Unlike
    // auth that one is a WARNING and not a gate -- an unprovisioned hub still
    // arms, still logs peers in, and still talks to its DIRECT peers, which is
    // every message this test sends. So the warning was inert here; it is
    // turned off only so a clean run prints no diagnostics at all. (It renders
    // as "[ERROR]" because the stderr fallback labels every class that is not
    // DEBUG or TRACE that way -- see Msgcore/Msgexception.cpp.)
    oHubA.RequireSeal(false);
    oHubB.RequireSeal(false);
    oHubC.RequireSeal(false);

    HANDLE hA = oHubA.SpawnHub();
    HANDLE hB = oHubB.SpawnHub();
    HANDLE hC = oHubC.SpawnHub();
    if (!hA || !hB || !hC)
    { wprintf(L"FATAL: SpawnHub() failed.\n"); return 1; }
    LogAt(L"MAIN", L"three hubs spawned");

    int nExit = 0;

    // ---- Post the two SERVICE (listening) connections ------------------
    // WSA service on HubB, expecting peer HubA.
    P2PeerConWsa*  pWsaSvc  = P2PeerConWsa::ServiceFactory(kHubA, kPort);
    // Pipe service on HubC, expecting peer HubA.
    P2PeerConPipe* pPipeSvc = P2PeerConPipe::ServiceFactory(kHubA, kPipeName);
    if (!pWsaSvc || !pPipeSvc)
    { wprintf(L"FATAL: a ServiceFactory failed.\n"); return 1; }

    BOOL bWsaSvc  = oHubB.PostP2PeerCon(pWsaSvc);
    BOOL bPipeSvc = oHubC.PostP2PeerCon(pPipeSvc);
    wprintf(L"  HubB.PostP2PeerCon(WSA  service) -> %s\n", bWsaSvc  ? L"TRUE" : L"FALSE");
    wprintf(L"  HubC.PostP2PeerCon(Pipe service) -> %s\n", bPipeSvc ? L"TRUE" : L"FALSE");
    fflush(stdout);

    // Let both services arm (pipe must exist before the client opens it;
    // WSA must be listening before connect).
    Sleep(750);

    // ---- Post BOTH client connections onto the SAME hub (HubA) ---------
    // This is the crux: one hub, two different transports, at the same time.
    P2PeerConWsa*  pWsaCli  = P2PeerConWsa::ClientFactory(kHubB, L"127.0.0.1", kPort);
    P2PeerConPipe* pPipeCli = P2PeerConPipe::ClientFactory(kHubC, kPipeName);
    if (!pWsaCli || !pPipeCli)
    { wprintf(L"FATAL: a ClientFactory failed.\n"); return 1; }

    BOOL bWsaCli  = oHubA.PostP2PeerCon(pWsaCli);    // WSA con onto HubA
    BOOL bPipeCli = oHubA.PostP2PeerCon(pPipeCli);   // pipe con onto HubA
    wprintf(L"  HubA.PostP2PeerCon(WSA  client, that='%s') -> %s\n",
            kHubB, bWsaCli  ? L"TRUE" : L"FALSE");
    wprintf(L"  HubA.PostP2PeerCon(Pipe client, that='%s') -> %s\n",
            kHubC, bPipeCli ? L"TRUE" : L"FALSE");
    fflush(stdout);

    if (!bWsaSvc || !bPipeSvc || !bWsaCli || !bPipeCli)
    {
        LogAt(L"MAIN", L"FAIL: a post was rejected");
        nExit = 3;
    }
    else
    {
        LogAt(L"MAIN", L"HubA now holds a WSA con AND a pipe con -- waiting for both acks (12s)");
        DWORD dw = WaitForSingleObject(g_hBothAck, 12000);
        if (dw == WAIT_OBJECT_0)
        {
            LogAt(L"MAIN", L"HubA completed login on BOTH transports");

            // Login is up; each transport's login-ack already posted a request.
            // Now wait for a response to come back over BOTH the socket and pipe.
            LogAt(L"MAIN", L"waiting for a PostP2PeerMsg response on BOTH transports (12s)");
            DWORD dwRsp = WaitForSingleObject(g_hBothRsp, 12000);
            if (dwRsp == WAIT_OBJECT_0)
                LogAt(L"MAIN", L"HubA received a response over BOTH transports");
            else
            {
                wprintf(L"  WSA rsp=%ld  Pipe rsp=%ld\n", g_nWsaRsp, g_nPipeRsp);
                LogAt(L"MAIN", L"FAIL: a response did not come back in time");
                nExit = 3;
            }
        }
        else
        {
            wprintf(L"  WSA ack=%ld  Pipe ack=%ld\n", g_nWsaAck, g_nPipeAck);
            LogAt(L"MAIN", L"FAIL: a transport did not complete login in time");
            nExit = 3;
        }
    }

    if (nExit == 0)
        LogAt(L"MAIN", L"VERDICT: PASS -- one hub (HubA) holds a live P2PeerConWsa "
                       L"AND a live P2PeerConPipe, and exchanged a request/response "
                       L"over each");
    else
        LogAt(L"MAIN", L"VERDICT: FAIL -- see lines above");

    // ---- Shutdown (clients' hub first, then services, kernel last) -----
    LogAt(L"MAIN", L"shutdown begin");
    oHubA.CloseHub();
    oHubB.CloseHub();
    oHubC.CloseHub();
    WaitForSingleObject(hA, 3000);
    WaitForSingleObject(hB, 3000);
    WaitForSingleObject(hC, 3000);
    CloseHandle(hA); CloseHandle(hB); CloseHandle(hC);

    CleanupP2Pmsg();
    if (g_hBothAck) { CloseHandle(g_hBothAck); g_hBothAck = NULL; }
    if (g_hBothRsp) { CloseHandle(g_hBothRsp); g_hBothRsp = NULL; }
    WSACleanup();

    wprintf(L"Done (exit=%d).\n", nExit);
    fflush(stdout);
    return nExit;
}

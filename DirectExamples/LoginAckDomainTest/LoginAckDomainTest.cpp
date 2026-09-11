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
// LoginAckDomainTest.cpp
//
// Covers P2PeerCon::LoginAck's domain check -- the one the rest of this
// suite does not reach.
//
// WHY THIS HARNESS EXISTS
//   LoginAck() refuses an acknowledgement that assigns the connection a
//   P2Paddr its domain does not cover ("Attempted security breach"). That
//   test guards a deliberate bypass: the assignment it sits in front of is
//   allowed to skip the "Attempt to swap P2PeerID's" check below it,
//   because an ACCEPTED connection IS permitted to take the address the
//   acknowledgement carries. The domain test is the only thing standing
//   between that allowance and an arbitrary address.
//
//   It had been commented out. Enabling it exposed a second problem: NO
//   harness in DirectExamples reaches it. Measured, Debug|x64 -- replacing
//   the test with an unconditional throw still passed all 11 harnesses
//   with no refusal logged. A green suite said nothing about the check.
//
//   The reason is the address, not the path. The default handler chain
//   (P2PeerTarget::On_ConLogin -> OnLogin -> LoginAck) forwards whatever
//   address the peer sent, and the meshes send none, so LoginAck's
//   oThatP2Paddr.IsNull() is true and the block is skipped. Where an
//   address IS sent -- TwoConTest -- OnLogin's own domain check throws
//   first and LoginAck never runs at all.
//
//   So the check only matters where a custom handler acknowledges with an
//   address of its own choosing. That is exactly what this harness does.
//
// WHAT IT DOES
//   One hub, two P2PeerConWsa connections over TCP loopback, as TwoConTest
//   sets up. The accepted (spawn) connection inherits from its listener
//   (P2PeerCon::AcceptSpawn): mode P2PeerCon_Accept, m_oThatP2Paddr, and
//   m_oP2Padomain -- the three things LoginAck's outer test needs. The hub
//   overrides On_ConLogin and, instead of delegating, calls LoginAck
//   itself, twice:
//
//   PART A (negative): LoginAck(kAddrOutOfDomain) -- an address the
//                      inherited domain does not map. MUST throw P2Pevent.
//                      Nothing is mutated before the throw, so the
//                      connection is untouched and Part B can proceed.
//   PART B (positive): LoginAck(kAddrInDomain) -- the domain's own
//                      address. MUST NOT throw, and is the real
//                      acknowledgement that completes the handshake.
//
//   Running the negative first matters: a refusal leaves no ack on the
//   wire, so the positive is still the first and only one the peer sees.
//
// Verdict via EXIT CODE (unambiguous for a headless run):
//   0 = PASS : the probe ran on an accepted connection, the out-of-domain
//              acknowledgement was refused, and the in-domain one was not.
//   2 = an MFC/CRT assertion fired (banner printed).
//   3 = the probe never ran (the check is STILL uncovered -- read that as
//       loudly as a failure), or a refusal/acceptance went the wrong way.
//   1 = setup failure (startup / factory / SpawnHub).

#include "stdafx.h"
#include "LoginAckDomainTest.h"

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
static const short      kPort     = 7793;

static const P2PaddrSTR kHubAddr  = L"LoginAckDom.Hub";

// The listening connection's "that" address. P2PeerCon's constructor copies
// it into m_oP2Padomain, and AcceptSpawn hands BOTH down to the accepted
// connection -- so this string is simultaneously the accepted connection's
// domain and its m_oThatP2Paddr.
static const P2PaddrSTR kSvcPeer  = L"LoginAckDom.PeerA";
static const P2PaddrSTR kCliPeer  = L"LoginAckDom.PeerB";

// P2Paddr::IsMapped is a wildcard glob, and kSvcPeer carries no wildcard,
// so it maps itself and nothing else. That makes both cases exact.
static const P2PaddrSTR kAddrInDomain    = L"LoginAckDom.PeerA";
static const P2PaddrSTR kAddrOutOfDomain = L"LoginAckDom.Rogue";

// Probe outcome, written on the hub thread, read on main after the event.
static HANDLE g_hProbeDone       = NULL;
static bool   g_bProbeRan        = false;
static bool   g_bNegativeRefused = false;
static bool   g_bPositiveAllowed = false;

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
// LoginAckDomainHub
//   Overrides On_ConLogin and does NOT delegate to the base. Delegating is
//   what makes the check unreachable everywhere else: the base forwards the
//   peer's address to OnLogin first, whose own domain check throws before
//   LoginAck is ever called.
// =========================================================================
class LoginAckDomainHub : public P2PeerHub
{
public:
    LoginAckDomainHub(P2PaddrSTR strAddr) : P2PeerHub(strAddr) {}
    virtual ~LoginAckDomainHub() {}

protected:
    virtual conRESULT On_ConLogin(P2PeerCon* pCon,
                                  P2PaddrSTR  strThatAddr,
                                  const void* pvLoginMsg,
                                  P2Psize_t   iSize) override
    {
        Trace(L"On_ConLogin", pCon);

        // Only the accepted connection carries the three things LoginAck's
        // outer test needs. Anything else goes down the ordinary path.
        if (!pCon || pCon->GetMode() != P2PeerCon_Accept || g_bProbeRan)
            return P2PeerHub::On_ConLogin(pCon, strThatAddr, pvLoginMsg, iSize);

        g_bProbeRan = true;
        LogAt(L"HUB", L"probe: accepted connection reached, exercising LoginAck");

        // ---- PART A (negative) ------------------------------------------
        // An acknowledgement assigning an address outside the inherited
        // domain must be refused.
        try
        {
            pCon->LoginAck(kAddrOutOfDomain, 0, 0);
            g_bNegativeRefused = false;      // returned == not refused
            LogAt(L"HUB", L"PART A FAILED: out-of-domain LoginAck was ALLOWED");
        }
        catch (P2Pevent* pEVT)
        {
            g_bNegativeRefused = true;
            LogAt(L"HUB", L"PART A: out-of-domain LoginAck refused (as it must be)");
            if (pEVT) pEVT->Cancel();
        }

        // ---- PART B (positive) ------------------------------------------
        // The domain's own address must pass, and this is the real
        // acknowledgement that completes the handshake.
        try
        {
            pCon->LoginAck(kAddrInDomain, 0, 0);
            g_bPositiveAllowed = true;
            LogAt(L"HUB", L"PART B: in-domain LoginAck allowed (as it must be)");
        }
        catch (P2Pevent* pEVT)
        {
            g_bPositiveAllowed = false;
            LogAt(L"HUB", L"PART B FAILED: in-domain LoginAck was REFUSED");
            if (pEVT) pEVT->Cancel();
        }

        if (g_hProbeDone) SetEvent(g_hProbeDone);
        return conHANDLED;
    }

    virtual conRESULT On_ConAccept(P2PeerCon* pCon) override
    { Trace(L"On_ConAccept", pCon); return P2PeerHub::On_ConAccept(pCon); }

    virtual conRESULT On_ConListen(P2PeerCon* pCon) override
    { Trace(L"On_ConListen", pCon); return P2PeerHub::On_ConListen(pCon); }

    virtual conRESULT On_ConConnect(P2PeerCon* pCon) override
    { Trace(L"On_ConConnect", pCon); return P2PeerHub::On_ConConnect(pCon); }

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

    wprintf(L"=== LoginAckDomainTest -- P2PeerCon::LoginAck domain check ===\n");
    wprintf(L"Hub       : %s\n", kHubAddr);
    wprintf(L"Domain    : %s (the listener's 'that' address)\n", kSvcPeer);
    wprintf(L"In-domain : %s\n", kAddrInDomain);
    wprintf(L"Out-of-dom: %s\n", kAddrOutOfDomain);
    wprintf(L"Port      : %d (TCP loopback 127.0.0.1)\n\n", (int)kPort);
    fflush(stdout);

    g_hProbeDone = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (!StartupP2Pmsg(16))
    { wprintf(L"FATAL: StartupP2Pmsg() failed.\n"); return 1; }

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    { wprintf(L"FATAL: WSAStartup() failed.\n"); return 1; }

    LoginAckDomainHub oHub(kHubAddr);
    //  As TwoConTest: this example provisions no identity and no allow-list,
    //  so a hub that requires authentication would refuse to arm. NOT the
    //  posture to copy into a real hub.
    oHub.RequireAuth(false);

    HANDLE hThread = oHub.SpawnHub();
    if (!hThread)
    { wprintf(L"FATAL: SpawnHub() failed.\n"); return 1; }
    LogAt(L"MAIN", L"hub spawned");

    int nExit = 0;

    // The listening connection. Its "that" address becomes the domain that
    // the accepted connection inherits and that LoginAck tests against.
    P2PeerConWsa* pConServer = P2PeerConWsa::ServiceFactory(kSvcPeer, kPort);
    if (!pConServer)
    { wprintf(L"FATAL: ServiceFactory failed.\n"); return 1; }
    if (!oHub.PostP2PeerCon(pConServer))
    { wprintf(L"FATAL: PostP2PeerCon(server) rejected.\n"); return 1; }

    // Let the hub arm listen/accept before the client dials.
    Sleep(750);

    P2PeerConWsa* pConClient =
        P2PeerConWsa::ClientFactory(kCliPeer, L"127.0.0.1", kPort);
    if (!pConClient)
    { wprintf(L"FATAL: ClientFactory failed.\n"); return 1; }
    if (!oHub.PostP2PeerCon(pConClient))
    { wprintf(L"FATAL: PostP2PeerCon(client) rejected.\n"); return 1; }

    LogAt(L"MAIN", L"both connections posted -- waiting for the login probe");

    DWORD dw = WaitForSingleObject(g_hProbeDone, 15000);

    // ===================================================================
    // Verdict
    // ===================================================================
    if (dw != WAIT_OBJECT_0 || !g_bProbeRan)
    {
        LogAt(L"MAIN", L"FAIL: the probe never ran -- LoginAck's domain "
                       L"check is STILL not covered by this suite");
        nExit = 3;
    }
    else if (!g_bNegativeRefused)
    {
        LogAt(L"MAIN", L"FAIL: an out-of-domain LoginAck was accepted");
        nExit = 3;
    }
    else if (!g_bPositiveAllowed)
    {
        LogAt(L"MAIN", L"FAIL: an in-domain LoginAck was refused");
        nExit = 3;
    }
    else
    {
        LogAt(L"MAIN", L"VERDICT: PASS -- out-of-domain LoginAck refused, "
                       L"in-domain LoginAck allowed");
    }

    LogAt(L"MAIN", L"shutdown begin");
    oHub.CloseHub();
    WaitForSingleObject(hThread, 3000);
    CloseHandle(hThread);

    CleanupP2Pmsg();
    if (g_hProbeDone) { CloseHandle(g_hProbeDone); g_hProbeDone = NULL; }
    WSACleanup();

    wprintf(L"Done (exit=%d).\n", nExit);
    fflush(stdout);
    return nExit;
}

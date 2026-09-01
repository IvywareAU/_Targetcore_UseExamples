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
// PipeMsgFactoryTest.cpp
//
// FACTORY-BASED variant of PipeMsgMapTest.
//
// Same single-process, two-hub, named-pipe (P2PeerConPipe) request/response
// round trip routed by a P2PeerMsg_MAP -- but the two SEND sites no longer
// hand-construct a P2PeerMsg32.  Instead they emit the targeted (named)
// message through the framework's *factory* API and prove it routes to the
// exact same ON_P2PeerMsg handlers:
//
//   * CLIENT ping     : P2PeerMsg::RedirectFactory(dst, name, data, size)
//                       - builds a routable, caller-owned message addressed to
//                         a named peer from a seed template.  This is the same
//                         pattern the framework itself uses in
//                         P2PeerExplorer.cpp: PostP2PeerMsg(pMsg->RedirectFactory(dst)).
//
//   * SERVER reply    : P2PeerMsg::RedirectFactory(dst, name, data, size)
//                       - builds a FRESH message addressed back to the ping's
//                         sender with a new name + payload, from a server-
//                         sourced seed.
//
//   CAVEAT (why not ResponseFactory here): ResponseFactory reverses the
//   addressing but ALSO inherits the received message's routing prefix, so a
//   standalone PostP2PeerMsg() of it loops the reply back into the CURRENT
//   hub's map rather than routing it across the pipe.  The framework only ever
//   returns a ResponseFactory result to the pump (see P2PeerHub.cpp:1054); it
//   never standalone-posts one.  RedirectFactory is the safe cross-hub choice.
//
// Both factories return a message the caller owns; both are injected with the
// ordinary PostP2PeerMsg().  Routing, map matching and verification are
// identical to PipeMsgMapTest -- exit code 0 means the factory-built messages
// reached On_HubPing / On_HubPong just like the hand-built ones did.
//
//   client --HubPing(RedirectFactory)--> [pipe] --> server.On_HubPing
//   server --HubPong(RedirectFactory)--> [pipe] --> client.On_HubPong  => DONE
//
// Verdict is reported by process EXIT CODE:
//   0 = SUCCESS : client received the server's HubPong reply.
//   2 = ASSERT  : an MFC/CRT assertion fired (banner printed).
//   3 = TIMEOUT : no reply delivered in time (handshake/routing did not complete).
//   1 = SETUP   : startup/factory failure.

#include "stdafx.h"
#include "PipeMsgFactoryTest.h"

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
// Mesh configuration  (distinct pipe/addresses so this can run alongside
// PipeMsgMapTest without colliding on the kernel pipe name).
// -------------------------------------------------------------------------
static LPCTSTR            kPipeName   = _T("\\\\.\\pipe\\P2PmsgFactoryProbe");
static const P2PaddrSTR   kServerAddr = L"MsgFac.Server";
static const P2PaddrSTR   kClientAddr = L"MsgFac.Client";

// Application-defined P2PeerMsg names routed by the P2PeerMsg_MAP.
#define kMsgPing  L"HubPing"
#define kMsgPong  L"HubPong"

// Signalled by the CLIENT hub once it receives the server's HubPong reply.
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
// Assertion trap: turn a modal debug ASSERT into a deterministic exit(2).
// -------------------------------------------------------------------------
static int __cdecl AssertReportHook(int nReportType, char* szMsg, int* pnRet)
{
    if (nReportType == _CRT_ASSERT)
    {
        fflush(stdout);
        fprintf(stderr,
                "\n=== ASSERT TRIPPED (pipe factory round trip) ===\n%s\n",
                szMsg ? szMsg : "(no message)");
        fflush(stderr);
        if (pnRet) *pnRet = 0;   // don't invoke the debugger
        ExitProcess(2);
    }
    return FALSE;                // let other report types flow normally
}

// =========================================================================
// FactoryMapHub
//   One P2PeerHub subclass used for BOTH ends, carrying a P2PeerMsg_MAP with
//   a handler per application message.  Delivery is by destination address:
//     - the SERVER receives HubPing  -> On_HubPing  (replies via ResponseFactory)
//     - the CLIENT receives HubPong  -> On_HubPong  (signals done)
// =========================================================================
class FactoryMapHub : public P2PeerHub
{
    // Registers this class's P2PeerMsg_MAP (implemented below, at file scope).
    DECLARE_P2PeerMsg_MAP()

public:
    FactoryMapHub(P2PaddrSTR strAddr, bool bServer)
        : P2PeerHub(strAddr)
        , m_bServer(bServer)
        , m_bSent(false)
    {}
    virtual ~FactoryMapHub() {}

protected:
    // ---- P2PeerMsg_MAP handlers -----------------------------------------

    // SERVER side: intercepts the client's HubPing and replies using the
    // framework FACTORY instead of a hand-built P2PeerMsg32.
    //
    //   NOTE: ResponseFactory() reverses the addressing BUT also inherits the
    //   received ping's envelope prefix (its resolved routing state) -- so a
    //   *standalone* PostP2PeerMsg() of it loops the reply back into THIS hub's
    //   map instead of routing it across the pipe.  (That is why the framework
    //   itself never standalone-posts a ResponseFactory result; it returns it
    //   to the pump.)  The safe cross-hub factory is RedirectFactory(dst,name,
    //   data,size), which builds a FRESH addressed message.  We seed a
    //   server-sourced template and redirect it to the ping's sender.
    msgRESULT On_HubPing(P2PeerMsg* pMsg)
    {
        PrintMessage(L"On_HubPing (recv)", pMsg);

        LPCWSTR   lpszReply = L"Pong: server got your ping, replying via RedirectFactory.";
        P2Psize_t nBytes    = (P2Psize_t)((wcslen(lpszReply) + 1) * sizeof(wchar_t));

        // Seed template: source = this server.  RedirectFactory then produces a
        // fresh message addressed (src=Server, dst=<ping sender>) with the pong
        // name + payload -- no inherited routing prefix.
        P2PeerMsg32 oSeed(kServerAddr, kServerAddr, kMsgPong, lpszReply, nBytes);
        P2PeerMsg*  pReply = oSeed.RedirectFactory(pMsg->GetSource(), kMsgPong, lpszReply, nBytes);

        wprintf(L"[SERVER] RedirectFactory reply src='%s' dst='%s'\n",
                pReply->GetSource(), pReply->GetDestin());
        fflush(stdout);

        PostP2PeerMsg(pReply);

        wprintf(L"[SERVER] Posted '%s' -> '%s'\n", kMsgPong, pMsg->GetSource());
        fflush(stdout);
        return msgHANDLED;
    }

    // CLIENT side: intercepts the server's HubPong reply -> round trip done.
    msgRESULT On_HubPong(P2PeerMsg* pMsg)
    {
        PrintMessage(L"On_HubPong (recv)", pMsg);
        if (g_hDoneEvent)
            SetEvent(g_hDoneEvent);
        return msgHANDLED;
    }

    // ---- Connection lifecycle -------------------------------------------

    // Once the CLIENT's login is acked, the pipe is ready: fire the ping.
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
            wprintf(L"[CLIENT] Login ack from '%s' - pipe ready, sending ping.\n",
                    strThatP2Paddr);
            PostPing();
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

    void PrintMessage(LPCWSTR lpszWhere, P2PeerMsg* pMsg)
    {
        LPCWSTR lpszName = pMsg ? pMsg->c_name()   : L"<null>";
        LPCWSTR lpszSrc  = pMsg ? pMsg->GetSource(): L"<null>";
        LPCWSTR lpszData = (pMsg && pMsg->Data() && pMsg->DataSize() > 0)
                         ? (LPCWSTR)pMsg->Data()
                         : L"<no data>";

        wprintf(L"\n[%s] %s  name='%s' from='%s'\n  > %s\n\n",
                m_bServer ? L"SERVER" : L"CLIENT",
                lpszWhere, lpszName, lpszSrc, lpszData);
        fflush(stdout);
    }

    // CLIENT: post the initial HubPing addressed to the SERVER hub, built via
    // the factory API rather than a direct P2PeerMsg32 ctor.
    //   Origination has no in-flight message to derive from, so we seed a
    //   template whose SOURCE is this client, then RedirectFactory produces the
    //   routable, caller-owned message addressed to the named server peer.
    //   (Mirrors PostP2PeerMsg(pMsg->RedirectFactory(dst)) in P2PeerExplorer.cpp.)
    void PostPing()
    {
        LPCWSTR   lpszMsg = L"Ping: hello Server, this is Client via RedirectFactory.";
        P2Psize_t nBytes  = (P2Psize_t)((wcslen(lpszMsg) + 1) * sizeof(wchar_t));

        // Seed template: source = this client (destination is overwritten by
        // the factory to the named target below).
        P2PeerMsg32 oSeed(kClientAddr, kClientAddr, kMsgPing, lpszMsg, nBytes);

        // Factory build: addressed (src=Client, dst=Server), named, caller-owned.
        P2PeerMsg* pMsg = oSeed.RedirectFactory(kServerAddr, kMsgPing, lpszMsg, nBytes);

        PostP2PeerMsg(pMsg);
        wprintf(L"[CLIENT] Posted '%s' -> '%s' (RedirectFactory): \"%s\"\n",
                kMsgPing, kServerAddr, lpszMsg);
        fflush(stdout);
    }

private:
    bool m_bServer;
    bool m_bSent;
};

// -------------------------------------------------------------------------
// P2PeerMsg_MAP for FactoryMapHub -- identical to PipeMsgMapTest: the SEND
// side changed, not the routing/interception side.
// -------------------------------------------------------------------------
BEGIN_P2PeerMsg_MAP(FactoryMapHub, P2PeerHub)
    ON_P2PeerMsg(kMsgPing, On_HubPing)   // server intercepts the request
    ON_P2PeerMsg(kMsgPong, On_HubPong)   // client intercepts the reply
END_P2PeerMsg_MAP()


// =========================================================================
// main  (identical harness to PipeMsgMapTest so output is directly comparable)
// =========================================================================
int main(int /*argc*/, char* /*argv*/[])
{
    _setmode(_fileno(stdout), _O_U16TEXT);

    // Route asserts to our hook (no modal dialog / no debugger break).
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportHook(AssertReportHook);

    wprintf(L"=== PipeMsgFactoryTest - two hubs, named pipe, factory-based sends ===\n");
    wprintf(L"Pipe   : %s\n", kPipeName);
    wprintf(L"Server : %s\n", kServerAddr);
    wprintf(L"Client : %s\n\n", kClientAddr);
    fflush(stdout);

    g_hDoneEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (!StartupP2Pmsg(16))
    {
        wprintf(L"FATAL: StartupP2Pmsg() failed.\n");
        return 1;
    }
    // Named pipes don't need Winsock, but base classes may touch it.
    WSADATA oWsaData;
    WSAStartup(MAKEWORD(2, 2), &oWsaData);

    // ---- Hub A: SERVER (creates + listens on the named pipe) -------------
    FactoryMapHub oServer(kServerAddr, /*bServer*/ true);
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

    // Give the server pump a moment to create the pipe + post its overlapped
    // ConnectNamedPipe wait before the client CreateFile(OPEN_EXISTING).
    Sleep(750);

    // ---- Hub B: CLIENT (opens the existing named pipe) -------------------
    FactoryMapHub oClient(kClientAddr, /*bServer*/ false);
    oClient.RequireAuth ( false );          // as above - unprovisioned example
    HANDLE hClientThread = oClient.SpawnHub();
    if (!hClientThread) { wprintf(L"FATAL: client SpawnHub failed.\n"); return 1; }
    LogAt(L"CLIENT", L"hub thread started");

    P2PeerConPipe* pCliCon = P2PeerConPipe::ClientFactory(kServerAddr, kPipeName);
    if (!pCliCon) { wprintf(L"FATAL: ClientFactory failed.\n"); return 1; }
    oClient.PostP2PeerCon(pCliCon);
    LogAt(L"CLIENT", L"client pipe connection posted (CreateFile OPEN_EXISTING)");

    // ---- Wait for the round trip -----------------------------------------
    LogAt(L"MAIN", L"waiting up to 10s for HubPing -> HubPong round trip...");
    DWORD dwResult = WaitForSingleObject(g_hDoneEvent, 10000);

    int nExit;
    if (dwResult == WAIT_OBJECT_0)
    {
        LogAt(L"MAIN", L"SUCCESS - client received the server's HubPong reply");
        nExit = 0;
    }
    else
    {
        LogAt(L"MAIN", L"TIMEOUT - no reply delivered (round trip did not complete)");
        nExit = 3;
    }

    // ---- Shutdown (clients/servers down, threads joined, kernel last) -----
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

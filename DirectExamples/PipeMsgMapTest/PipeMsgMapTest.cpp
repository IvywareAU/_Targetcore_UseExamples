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
// PipeMsgMapTest.cpp
//
// SINGLE-PROCESS, TWO-HUB named-pipe request/response probe for Targetcore,
// demonstrating P2PeerMsg_MAP routing of application-defined messages.
//
// What this harness shows:
//   Two P2PeerHub's live in ONE process, joined by a Windows NAMED PIPE
//   (P2PeerConPipe).  Once the login handshake completes, the CLIENT hub
//   posts a named message ("HubPing") addressed to the SERVER hub.  The
//   framework routes that message across the pipe to the server, whose
//   P2PeerMsg_MAP intercepts it in On_HubPing and, in the handler, generates
//   an additional P2PeerMsg ("HubPong") addressed back to the client for
//   subsequent routing.  The CLIENT hub's map intercepts the reply in
//   On_HubPong and the round trip is complete.
//
//   client --HubPing--> [pipe] --> server.On_HubPing
//   server --HubPong--> [pipe] --> client.On_HubPong  => DONE
//
// Routing rule exercised (verbatim from the Targetcore contract):
//   "Any P2PeerMsg objects for which the destination address matches the hub
//    address are pumped through the hub P2PeerTarget hierarchy until a
//    matching handler is located."
//   Here the matching handlers are supplied by BEGIN_P2PeerMsg_MAP /
//   ON_P2PeerMsg / END_P2PeerMsg_MAP on the PipeMapHub class -- no virtual
//   On_P2PeerBCast/UCast override is used; delivery is by name, through the map.
//
// Verdict is reported by process EXIT CODE (so a headless run is unambiguous):
//   0 = SUCCESS : client received the server's HubPong reply.
//   2 = ASSERT  : an MFC/CRT assertion fired (banner printed).
//   3 = TIMEOUT : no reply delivered in time (handshake/routing did not complete).
//   1 = SETUP   : startup/factory failure.

#include "stdafx.h"
#include "PipeMsgMapTest.h"

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
static LPCTSTR            kPipeName   = _T("\\\\.\\pipe\\P2PmsgMapProbe");
static const P2PaddrSTR   kServerAddr = L"MsgMap.Server";
static const P2PaddrSTR   kClientAddr = L"MsgMap.Client";

// Application-defined P2PeerMsg names routed by the P2PeerMsg_MAP.
// NOTE: message names are wide strings (P2PmsgID == LPCWSTR); the SAME literal
//       is used both in the map (ON_P2PeerMsg) and in the P2PeerMsg32 ctor.
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
                "\n=== ASSERT TRIPPED (pipe msg-map round trip) ===\n%s\n",
                szMsg ? szMsg : "(no message)");
        fflush(stderr);
        if (pnRet) *pnRet = 0;   // don't invoke the debugger
        ExitProcess(2);
    }
    return FALSE;                // let other report types flow normally
}

// =========================================================================
// PipeMapHub
//   One P2PeerHub subclass used for BOTH ends.  It carries a P2PeerMsg_MAP
//   with a handler for each application message.  Because delivery is by
//   destination address, only the relevant handler fires per role:
//     - the SERVER receives HubPing  -> On_HubPing  (and replies with HubPong)
//     - the CLIENT receives HubPong  -> On_HubPong  (and signals done)
//   The unused handler on each end is simply never routed to.
// =========================================================================
class PipeMapHub : public P2PeerHub
{
    // Registers this class's P2PeerMsg_MAP (implemented below, at file scope).
    DECLARE_P2PeerMsg_MAP()

public:
    PipeMapHub(P2PaddrSTR strAddr, bool bServer)
        : P2PeerHub(strAddr)
        , m_bServer(bServer)
        , m_bSent(false)
    {}
    virtual ~PipeMapHub() {}

protected:
    // ---- P2PeerMsg_MAP handlers -----------------------------------------

    // SERVER side: intercepts the client's HubPing, then GENERATES an
    // additional P2PeerMsg (HubPong) addressed back to the sender for
    // subsequent routing across the pipe.
    msgRESULT On_HubPing(P2PeerMsg* pMsg)
    {
        PrintMessage(L"On_HubPing (recv)", pMsg);

        LPCWSTR   lpszReply = L"Pong: server got your ping, replying over the pipe.";
        P2Psize_t nBytes    = (P2Psize_t)((wcslen(lpszReply) + 1) * sizeof(wchar_t));

        // Reply source = this hub; destination = whoever sent the ping.
        P2PeerMsg32* pReply = new P2PeerMsg32(
            kServerAddr, pMsg->GetSource(), kMsgPong, lpszReply, nBytes);

        PostP2PeerMsg(pReply);

        wprintf(L"[SERVER] Generated reply '%s' -> '%s'\n", kMsgPong, pMsg->GetSource());
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

    // CLIENT: post the initial HubPing addressed to the SERVER hub.
    void PostPing()
    {
        LPCWSTR   lpszMsg = L"Ping: hello Server, this is Client over a named pipe.";
        P2Psize_t nBytes  = (P2Psize_t)((wcslen(lpszMsg) + 1) * sizeof(wchar_t));

        P2PeerMsg32* pMsg = new P2PeerMsg32(
            kClientAddr, kServerAddr, kMsgPing, lpszMsg, nBytes);

        PostP2PeerMsg(pMsg);
        wprintf(L"[CLIENT] Posted '%s' -> '%s': \"%s\"\n",
                kMsgPing, kServerAddr, lpszMsg);
        fflush(stdout);
    }

private:
    bool m_bServer;
    bool m_bSent;
};

// -------------------------------------------------------------------------
// P2PeerMsg_MAP for PipeMapHub.
//   Both application messages are mapped by name.  A message whose
//   destination matches this hub is pumped through this map until one of
//   these ON_P2PeerMsg entries matches; the base P2PeerHub map still handles
//   framework messages (BCast/Error/...) beneath these.
// -------------------------------------------------------------------------
BEGIN_P2PeerMsg_MAP(PipeMapHub, P2PeerHub)
    ON_P2PeerMsg(kMsgPing, On_HubPing)   // server intercepts the request
    ON_P2PeerMsg(kMsgPong, On_HubPong)   // client intercepts the reply
END_P2PeerMsg_MAP()


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

    wprintf(L"=== PipeMsgMapTest - two hubs, named pipe, P2PeerMsg_MAP round trip ===\n");
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
    PipeMapHub oServer(kServerAddr, /*bServer*/ true);
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
    PipeMapHub oClient(kClientAddr, /*bServer*/ false);
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

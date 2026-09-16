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
// AlexTest.cpp
//
// Simple P2P messaging console app using Targetcore TCP/IP transport.
//
// Usage:
//   AlexTest                          -- server/receive mode  (listens on port 7777)
//   AlexTest send [ip] [message]      -- client/send mode     (connects and sends one message)
//
// Both instances must be running simultaneously: start the server first, then the client.

#include "stdafx.h"
#include "AlexTest.h"

#include <io.h>
#include <fcntl.h>

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

// Delay-load failure diagnostics come from vsutils\DelayLoadReport.cpp, which
// is compiled into this .exe (see the .vcxproj).  Without it, a missing or
// stale Targetcore DLL kills the process silently -- see that file's header.

// -------------------------------------------------------------------------
// Network configuration
// -------------------------------------------------------------------------
static const short        kTestPort   = 7777;
static const P2PaddrSTR   kServerAddr = L"AlexTest.Server";
static const P2PaddrSTR   kClientAddr = L"AlexTest.Client";

// Signals either "message posted" (client) or "message received" (server)
static HANDLE g_hDoneEvent = NULL;

// Wall-clock + PID stamped milestone log. Absolute timestamps make it
// unambiguous whether two process runs actually overlapped in time.
static void LogAt(LPCWSTR lpszRole, LPCWSTR lpszMsg)
{
    SYSTEMTIME st; GetLocalTime(&st);
    wprintf(L"[%02d:%02d:%02d.%03d pid=%lu %s] %s\n",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            GetCurrentProcessId(), lpszRole, lpszMsg);
    fflush(stdout);
}

// =========================================================================
// AlexTestHub
//
// Minimal P2PeerHub subclass that:
//   - In server mode : prints any arriving BCast or UCast message.
//   - In client mode : sends one text message once the TCP login completes.
// =========================================================================
class AlexTestHub : public P2PeerHub
{
public:
    AlexTestHub(P2PaddrSTR strAddr, bool bServer, LPCWSTR lpszMsg = NULL)
        : P2PeerHub(strAddr)
        , m_bServer(bServer)
        , m_bSent(false)
    {
        if (lpszMsg)
            m_strMsg = lpszMsg;
    }
    virtual ~AlexTestHub() {}

protected:
    // ------------------------------------------------------------------
    // Broadcast message handler.
    // Called when a P2PmsgBCast-typed message is routed to this hub.
    // ------------------------------------------------------------------
    virtual msgRESULT On_P2PeerBCast(P2PeerMsg* pMsg) override
    {
        PrintMessage(L"BCast", pMsg);
        return msgHANDLED;
    }

    // ------------------------------------------------------------------
    // Unicast handler.
    // Called for any message addressed specifically to this hub that
    // does not match a more specific map entry.
    // ------------------------------------------------------------------
    virtual msgRESULT On_P2PeerUCast(P2PeerMsg* pMsg) override
    {
        PrintMessage(L"UCast", pMsg);
        return msgHANDLED;
    }

    // ------------------------------------------------------------------
    // Connection login-acknowledged handler (client side).
    // The TCP+Targetcore handshake is complete; safe to post messages now.
    // ------------------------------------------------------------------
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
            wprintf(L"[CLIENT] Login ack from '%s' - connection ready.\n", strThatP2Paddr);
            PostTestMessage();
            m_bSent = true;
        }
        return result;
    }

    // ------------------------------------------------------------------
    // Connection-lifecycle trace overrides.
    // Each prints the stage reached then delegates to the base handler,
    // so the console shows exactly how far the login handshake gets on
    // each side before it stalls.
    // ------------------------------------------------------------------
    virtual conRESULT On_ConStartup(P2PeerCon* pCon) override
    {
        Trace(L"On_ConStartup", pCon);
        return P2PeerHub::On_ConStartup(pCon);
    }

    virtual conRESULT On_ConConnect(P2PeerCon* pCon) override
    {
        Trace(L"On_ConConnect", pCon);
        return P2PeerHub::On_ConConnect(pCon);
    }

    virtual conRESULT On_ConAccept(P2PeerCon* pCon) override
    {
        Trace(L"On_ConAccept", pCon);
        return P2PeerHub::On_ConAccept(pCon);
    }

    virtual conRESULT On_ConListen(P2PeerCon* pCon) override
    {
        Trace(L"On_ConListen", pCon);
        return P2PeerHub::On_ConListen(pCon);
    }

    virtual conRESULT On_ConLogin(P2PeerCon* pCon,
                                   P2PaddrSTR  strThatP2Paddr,
                                   const void* pvLoginMsg,
                                   P2Psize_t   iSize) override
    {
        Trace(L"On_ConLogin", pCon);
        return P2PeerHub::On_ConLogin(pCon, strThatP2Paddr, pvLoginMsg, iSize);
    }

    virtual conRESULT On_ConClose(P2PeerCon* pCon) override
    {
        Trace(L"On_ConClose", pCon);
        return P2PeerHub::On_ConClose(pCon);
    }

    virtual conRESULT On_ConShutdown(P2PeerCon* pCon) override
    {
        Trace(L"On_ConShutdown", pCon);
        return P2PeerHub::On_ConShutdown(pCon);
    }

private:
    // Emit a flushed, timestamped connection-lifecycle trace line.
    void Trace(LPCWSTR lpszStage, P2PeerCon* pCon)
    {
        LPCWSTR lpszAddr = L"<n/a>";
        try { if (pCon) lpszAddr = (P2PaddrSTR)pCon->GetP2Paddress(); }
        catch (...) { lpszAddr = L"<err>"; }

        SYSTEMTIME st; GetLocalTime(&st);
        wprintf(L"[%02d:%02d:%02d.%03d pid=%lu %s] %-16s con=%p addr='%s'\n",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                GetCurrentProcessId(),
                m_bServer ? L"SERVER" : L"CLIENT",
                lpszStage, (void*)pCon, lpszAddr);
        fflush(stdout);
    }

    // Print an arriving message to stdout
    void PrintMessage(LPCWSTR lpszKind, P2PeerMsg* pMsg)
    {
        LPCWSTR lpszSrc  = pMsg ? pMsg->GetSource() : L"<null>";
        LPCWSTR lpszData = (pMsg && pMsg->Data() && pMsg->DataSize() > 0)
                         ? (LPCWSTR)pMsg->Data()
                         : L"<no data>";

        wprintf(L"\n[%s] %s from '%s':\n  > %s\n\n",
                m_bServer ? L"SERVER" : L"CLIENT",
                lpszKind, lpszSrc, lpszData);

        if (g_hDoneEvent)
            SetEvent(g_hDoneEvent);
    }

    // Construct and post a BCast message to the server
    void PostTestMessage()
    {
        if (m_strMsg.IsEmpty())
            m_strMsg = L"Hello from AlexTest client!";

        LPCWSTR    lpszMsg = m_strMsg.GetString();
        P2Psize_t  nBytes  = (P2Psize_t)((m_strMsg.GetLength() + 1) * sizeof(wchar_t));

        // P2PeerMsg32 pre-allocates a 32-byte VBLock address area (accommodates
        // typical hub addresses).  The hub takes ownership of the heap object.
        P2PeerMsg32* pMsg = new P2PeerMsg32(
            kClientAddr, kServerAddr,
            P2Pmsg_BCast,
            lpszMsg, nBytes);

        PostP2PeerMsg(pMsg);
        wprintf(L"[CLIENT] Posted: \"%s\"\n", lpszMsg);

        if (g_hDoneEvent)
            SetEvent(g_hDoneEvent);
    }

private:
    bool    m_bServer;
    bool    m_bSent;
    CString m_strMsg;
};


// =========================================================================
// main
// =========================================================================
int main(int argc, char* argv[])
{
    // Put stdout in UTF-16 mode so wprintf emits Unicode (em-dash etc.)
    // correctly instead of aborting mid-string on the default C locale.
    _setmode(_fileno(stdout), _O_U16TEXT);

    // ---- Parse command line -------------------------------------------------
    bool    bServer = true;
    CString strIP   = L"127.0.0.1";
    CString strMsg  = L"Hello from AlexTest client!";

    if (argc >= 2 && _stricmp(argv[1], "send") == 0)
    {
        bServer = false;
        if (argc >= 3) strIP  = argv[2];   // CString(char*) does ANSI->Unicode
        if (argc >= 4) strMsg = argv[3];
    }

    wprintf(L"=== AlexTest - P2P Messaging Demo ===\n");
    wprintf(L"Mode  : %s\n", bServer ? L"SERVER  (listen on port 7777)"
                                      : L"CLIENT  (send one message)");
    if (!bServer)
        wprintf(L"Target: %s:%d\n", strIP.GetString(), (int)kTestPort);
    wprintf(L"\n");



    // ---- Synchronisation event ---------------------------------------------
    g_hDoneEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    // ---- Initialise Targetcore ---------------------------------------------
    if (!StartupP2Pmsg(16))
    {
        wprintf(L"FATAL: StartupP2Pmsg() failed.\n");
        CloseHandle(g_hDoneEvent);
        WSACleanup();
        return 1;
    }

    // ---- Initialise Winsock ------------------------------------------------
    // Targetcore's StartupP2Pmsg() does NOT call WSAStartup (only
    // P2PeerService::Run() does).  Since AlexTest drives the hub directly,
    // the process must initialise Winsock itself before any socket use.
    WSADATA oWsaData;
    int nWsaErr = WSAStartup(MAKEWORD(2, 2), &oWsaData);
    if (nWsaErr != 0)
    {
        wprintf(L"FATAL: WSAStartup() failed (%d).\n", nWsaErr);
        return 1;
    }

    // ---- Create hub --------------------------------------------------------
    P2PaddrSTR strHubAddr = bServer ? kServerAddr : kClientAddr;
    AlexTestHub oHub(strHubAddr, bServer,
                     bServer ? NULL : strMsg.GetString());

    // Spawn the hub in a dedicated thread (returns the thread HANDLE)
    //  ARMING: RequireAuth defaults to ON since ProductionPlan.md Stage 3
    //  step 8, and a hub that requires authentication it cannot enforce
    //  REFUSES TO ARM - SpawnHub() returns NULL rather than starting and
    //  then turning every peer away. This example provisions no identity
    //  and no allow-list, so it takes the documented one-line migration
    //  and says so out loud. NOT the posture to copy into a real hub.
    oHub.RequireAuth ( false );
    HANDLE hHubThread = oHub.SpawnHub();
    if (!hHubThread)
    {
        wprintf(L"FATAL: SpawnHub() failed.\n");
        CleanupP2Pmsg();
        CloseHandle(g_hDoneEvent);
        WSACleanup();
        return 1;
    }
    wprintf(L"Hub  '%s' started.\n", strHubAddr);

    // ---- Create connection -------------------------------------------------
    P2PeerConWsa* pCon = NULL;

    if (bServer)
    {
        // Service connection: listen for the client on kTestPort.
        // strP2PaddrThat = expected remote peer's address.
        pCon = P2PeerConWsa::ServiceFactory(kClientAddr, kTestPort);
        if (pCon)
        {
            oHub.PostP2PeerCon(pCon);
            wprintf(L"Listening on port %d - press Enter to exit.\n\n",
                    (int)kTestPort);
        }
    }
    else
    {
        // Client connection: actively connect to the server.
        // strP2PaddrThat = the server hub's address (used during login).
        pCon = P2PeerConWsa::ClientFactory(kServerAddr,
                                            strIP.GetString(),
                                            kTestPort);
        if (pCon)
        {
            oHub.PostP2PeerCon(pCon);
            wprintf(L"Connecting to %s:%d ...\n", strIP.GetString(), (int)kTestPort);
        }
    }

    if (!pCon)
    {
        wprintf(L"FATAL: Failed to create P2PeerConWsa.\n");
        oHub.CloseHub();
        CloseHandle(hHubThread);
        CleanupP2Pmsg();
        CloseHandle(g_hDoneEvent);
        WSACleanup();
        return 1;
    }

    // ---- Run ---------------------------------------------------------------
    if (bServer)
    {
        // Block until the user presses Enter. The timestamp on the next
        // line proves the server was alive and waiting at this instant;
        // if it is immediately followed by the "unblocked" line with no
        // Enter pressed, stdin returned EOF (not a real wait).
        LogAt(L"SERVER", L"armed; blocking on getchar() - leave running");
        getchar();
        LogAt(L"SERVER", L"getchar() returned; unblocking -> shutdown");
    }
    else
    {
        LogAt(L"CLIENT", L"waiting up to 10s for handshake/post...");
        // Wait for the message to be posted (or timeout after 10 s)
        DWORD dwResult = WaitForSingleObject(g_hDoneEvent, 10000);
        if (dwResult == WAIT_TIMEOUT)
            LogAt(L"CLIENT", L"TIMEOUT - no login-ack within 10s");
        else
            LogAt(L"CLIENT", L"message delivered");

        // Allow a moment for the message to reach the server
        Sleep(1000);
    }

    // ---- Shutdown ----------------------------------------------------------
    LogAt(bServer ? L"SERVER" : L"CLIENT", L"shutdown begin");
    wprintf(L"Shutting down...\n");
    oHub.CloseHub();
    CloseHandle(hHubThread);

    CleanupP2Pmsg();
    CloseHandle(g_hDoneEvent);
    g_hDoneEvent = NULL;
    WSACleanup();

    wprintf(L"Done.\n");
    return 0;
}

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
// ExplorerTest.cpp
//
// SINGLE-PROCESS, TWO-HUB example of the TargetCore HUB EXPLORER -- the
// directory service that lets a peer ask a hub what it is and be told when
// that changes.
//
// Question this example answers:
//   How does an application stand up a P2PeerExplorer, and what does a client
//   have to do to talk to one?
//
// It mirrors WsaMeshTest's shape -- two P2PeerHub's in one process over
// loopback TCP, each on its own SpawnHub() pump thread -- and adds the one
// thing WsaMeshTest has not got: an EXPUMP on the server hub, and a client
// that speaks P2PexpumpCtrl to it.
//
// ---------------------------------------------------------------------------
// THE FIVE THINGS THAT ARE NOT LIKE THE OTHER HARNESSES
//
//  1. The Explorer is not automatic. P2PeerHub.cpp:1034-1040 states that
//     instantiating one is deliberately the APPLICATION's job, and the
//     message-driven CREATE/SPAWN/CLOSE path in the hub is commented out in
//     its entirety. P2PeerExpump_ACTIVATE() is the documented way in; it
//     constructs, spawns AND registers with the hub, and all three matter --
//     an expump the hub cannot see is an orphan nothing routes to.
//
//  2. The service connection is posted to the EXPUMP, not to the hub.
//     pExp->PostP2PeerCon(), not oServer.PostP2PeerCon(). The expump owns the
//     XCid slot connections and processes their logins in its own context.
//
//  3. The client must be ANONYMOUS. On_XCidConLogin (P2PeerExplorer.cpp:1302)
//     throws "Null P2Paddr expected" on any client that declares an address of
//     its own, then server-assigns "<hub>.XC%i" from a free slot. A peer
//     cannot choose its Explorer identity, by design.
//
//  4. The client must NOT delegate On_ConLoginAck to the base.
//     P2PeerTarget::On_ConLoginAck (P2PeerTarget.cpp:2552-2557) throws on a
//     non-empty acknowledgement, and the Explorer acknowledges with the slot
//     index (P2PeerExplorer.cpp:1305) -- so the stock handler cannot complete
//     an Explorer login at all. The override below does what the base does
//     minus that check, which is currently the price of admission.
//
//  5. The answer arrives through PeekP2PeerMsg, and MUST be consumed.
//     The pump peeks ahead of the map dispatch for any message addressed to
//     this hub (P2Pwin32.cpp:3112-3115), and after the LoginAck this hub IS
//     CEX.XC0. Returning anything but msgHANDLED reflects the message back as
//     an exception, which On_P2PmsgExp_CATCH reads as a dead sink and
//     de-registers -- so a client that ignores the answer silently unsubscribes
//     itself.
//
// ---------------------------------------------------------------------------
// THE COMMAND VOCABULARY
//
// One P2PexpumpCtrl message carries one command as a named field on its data
// node. The hub commands are:
//
//   QHub   ask once; the Explorer answers with a P2PexpumpHub addressed to you
//   RHub   the same answer, and register as a standing status sink
//   DHub   de-register; no answer
//
// (RCon/QCon/DCon and RPmp/QPmp/DPmp are the same three verbs over connections
// and pumps, and are not exercised here.)
//
// ONE COMMAND PER MESSAGE. On_P2PexpCtrl (P2PeerExplorer.cpp:1450-1601) loops
// over the data node's cursor but re-tests every command with Exists() against
// the WHOLE node on each pass, so a message carrying two commands executes the
// first one twice and the second one never.
//
// The "@Dsc" qualifier the source documents is written by passing TRUE for
// P3PmsgField_SERIALISE's bDscAttr -- it hangs a Dsc attribute off the command
// field. The Explorer parses it (:1462) and hands it to QueryP2PmsgExp_Hub as
// bVerbose, which that function DOES NOT USE (P2Pwin32.cpp:1388-1407, where the
// parameter is even renamed bRegister). This example sends both forms so the
// answers can be compared; expect them to be identical.
//
// ---------------------------------------------------------------------------
// WHAT THE ANSWER CARRIES
//
// NotifyP2PmsgExp_Hub (P2Pwin32.cpp:1348-1386) builds it: Machine and
// Executable, both with descriptions, and then the hub's own Serialise(0)
// appended whole. So the payload is the hub's property tree, and this example
// walks it with P2PmsgRecurs and prints every node.
//
// ---------------------------------------------------------------------------
// Verdict is reported by process EXIT CODE (so a headless run is unambiguous):
//   0 = SUCCESS : slot assigned, all three answers arrived and carried the
//                 hub directory, and DHub was honoured (no fourth answer).
//   1 = SETUP   : startup / factory / activation failure.
//   2 = ASSERT  : an MFC/CRT assertion fired (banner printed).
//   3 = TIMEOUT : no assert, but the exchange did not complete in time.

#include "stdafx.h"

#include <io.h>
#include <fcntl.h>
#include <crtdbg.h>
#include <string>

#include "P2Pwin32.h"
#include "P2Pmsg.h"
#include "MsgCurs.h"
#include "P2PeerHub.h"
#include "P2PeerExplorer.h"
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
//
// The hub is called CEX because the library still has one hard-coded name in
// it: P2PeerExplorer::PeekP2PeerMsg (P2PeerExplorer.cpp:935) widens its
// interception address for a P2PexpumpHub only when GetP2PaddrHub()=="CEX",
// and its author left a TODO there saying so. Any other name is untested
// ground, and an example is the wrong place to find out. See README.md.
// -------------------------------------------------------------------------
static const short      kDefaultPort = 7823;
static const P2PaddrSTR kHubAddr     = L"CEX";       // the Explorer-bearing hub
static const P2PaddrSTR kExpDomain   = L"CEX.XC*";   // the slots it hands out
static const P2PaddrSTR kAnonAddr    = L"";          // the client holds none

static const int        kMaxNodes    = 512;          // dump guard

// -------------------------------------------------------------------------
// State. Written on the client hub's pump thread, read by main after the
// matching event -- nothing is polled across threads.
// -------------------------------------------------------------------------
static HANDLE       g_hAssigned = NULL;   // the Explorer gave us a slot
static HANDLE       g_hAnswer   = NULL;   // a P2PexpumpHub arrived
static volatile LONG g_nAnswers = 0;      // how many have arrived
static std::wstring g_strMe;              // "CEX.XC0", assigned at LoginAck
static int          g_nNodesLast = 0;     // nodes in the most recent answer
static bool         g_bSawMachine    = false;
static bool         g_bSawExecutable = false;

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
// banner instead of a modal dialog. A modal assert is indistinguishable from
// a hang to anything running this headlessly.
// -------------------------------------------------------------------------
static int __cdecl AssertReportHook(int nReportType, char* szMsg, int* pnRet)
{
    if (nReportType == _CRT_ASSERT)
    {
        fflush(stdout);
        fwprintf(stderr, L"\n=== ASSERT TRIPPED ===\n%S\n", szMsg ? szMsg : "(no message)");
        fflush(stderr);
        if (pnRet) *pnRet = 0;              // don't invoke the debugger
        ExitProcess(2);
    }
    return FALSE;                           // let other report types flow
}

// -------------------------------------------------------------------------
// Print the hub directory the Explorer sent back.
//
// P2PmsgRecurs walks a P3PmsgItem's whole subtree depth-first, pre-order, in
// one loop. Two rules it enforces and this respects:
//
//   * Push() only on a plain field. On a list or a vect it throws "Attempt to
//     push non-P2PmsgItem environment" -- their elements are cells, not named
//     children, so there is nothing below them to stand on.
//   * c_wstr() and ToString() may hand back a pointer into a 16-slot
//     thread-local ring (Platform/p2pstr.h:629-651) rather than into the
//     store. Copy the value out before the next accessor call. On Win32 they
//     return the store pointer and stay valid, so this costs nothing here and
//     is the difference between working and not on the Linux port.
// -------------------------------------------------------------------------
static int DumpDirectory(P3PmsgItem& oRoot)
{
    int nNodes = 0;

    P2PmsgRecurs oRec(oRoot);
    while (!oRec.IsEoRecurs() && nNodes < kMaxNodes)
    {
        const bool  bField = oRec.IsField();
        std::wstring strName  = oRec.c_wstr();          // copy at once
        std::wstring strValue;

        if (bField)
        {
            try
            {
                LPCTSTR lpszValue = oRec.r_data().ToString();
                if (lpszValue) strValue = lpszValue;    // copy at once
            }
            catch (P2Pevent* pEVT) { strValue = L"<unreadable>"; pEVT->Cancel(); }
        }
        else if (oRec.IsList()) strValue = L"<list>";
        else if (oRec.IsVect()) strValue = L"<vect>";

        wprintf(L"           %-24s %s\n", strName.c_str(), strValue.c_str());
        nNodes++;

        if (strName == L"Machine")    g_bSawMachine    = true;
        if (strName == L"Executable") g_bSawExecutable = true;

        if (bField)
            oRec.Push();
        ++oRec;
    }
    fflush(stdout);

    return nNodes;
}

// =========================================================================
// The client. It holds no address of its own; the Explorer gives it one.
//
// Note what is NOT here: no P2PeerExplorer subclass anywhere in this example.
// The server runs a stock one. Overriding it is for an application that wants
// to see the registry or raise its own notifications (MscsUnitTests\
// p2p_expreg.cpp does both); asking a hub what it is needs none of that.
// =========================================================================
class ExplorerClientHub : public P2PeerHub
{
public:
    ExplorerClientHub(P2PaddrSTR strAddr) : P2PeerHub(strAddr) {}
    virtual ~ExplorerClientHub() {}

    // One command per message -- see the header comment.
    void SendCommand(LPCTNAM lpszCommand, BOOL bDsc, LPCWSTR lpszDescription)
    {
        P2PeerMsg* pMsg = new P2PeerMsg(MSG_P2PexpCtrl);
        pMsg->SetSource(g_strMe.c_str());       // the slot we were assigned
        pMsg->SetDestin(kHubAddr);              // the Explorer's hub

        // The command is a named field on the data node; its value is not
        // read. bDsc=TRUE hangs the "@Dsc" attribute off it.
        P3PmsgField_SERIALISE(pMsg->r_datn(), lpszCommand, L"1",
                              bDsc, lpszDescription);
        PostP2PeerMsg(pMsg);

        wprintf(L"\n[CLIENT] --> P2PexpumpCtrl { %s%s } from '%s'\n",
                lpszCommand, bDsc ? L"@Dsc" : L"", g_strMe.c_str());
        fflush(stdout);
    }

protected:
    // The answer lands here, ahead of the message map, and has to be consumed.
    virtual msgRESULT PeekP2PeerMsg(P2PeerMsg* pMsg) override
    {
        if (pMsg && pMsg->Map_MatchName(MSG_P2PexpHub))
        {
            std::wstring strSource = pMsg->GetSource();   // copy at once
            std::wstring strDestin = pMsg->GetDestin();

            LONG n = InterlockedIncrement(&g_nAnswers);
            wprintf(L"[CLIENT] <-- P2PexpumpHub #%ld  [%s] -> [%s]\n",
                    (long)n, strSource.c_str(), strDestin.c_str());

            g_nNodesLast = DumpDirectory(pMsg->r_datn());
            wprintf(L"[CLIENT]     %d nodes\n", g_nNodesLast);
            fflush(stdout);

            if (g_hAnswer) SetEvent(g_hAnswer);
            return msgHANDLED;
        }
        return P2PeerHub::PeekP2PeerMsg(pMsg);
    }

    // NOT delegated to the base on purpose -- reason 4 in the header comment.
    virtual conRESULT On_ConLoginAck(P2PeerCon*  pCon,
                                     P2PaddrSTR  strThisP2Paddr,
                                     P2PaddrSTR  strThatP2Paddr,
                                     const void* pvLoginAck,
                                     P2Psize_t   iSize) override
    {
        UNREFERENCED_PARAMETER(pvLoginAck);

        wprintf(L"[CLIENT] login acknowledged: this='%s' that='%s' (ack %d bytes)\n",
                strThisP2Paddr, strThatP2Paddr, (int)iSize);
        fflush(stdout);

        pCon->OnLoginAck(strThisP2Paddr, strThatP2Paddr);

        if (g_strMe.empty() && strThisP2Paddr && *strThisP2Paddr)
        {
            g_strMe = strThisP2Paddr;
            if (g_hAssigned) SetEvent(g_hAssigned);
        }
        return conHANDLED;
    }
};

// -------------------------------------------------------------------------
// Wait until at least nWanted answers have arrived, or give up.
// -------------------------------------------------------------------------
static bool WaitForAnswers(LONG nWanted, DWORD dwTotalMs)
{
    const DWORD dwSlice = 250;
    for (DWORD dwWaited = 0; dwWaited < dwTotalMs; dwWaited += dwSlice)
    {
        if (InterlockedExchangeAdd(&g_nAnswers, 0) >= nWanted)
            return true;
        WaitForSingleObject(g_hAnswer, dwSlice);
    }
    return InterlockedExchangeAdd(&g_nAnswers, 0) >= nWanted;
}

// =========================================================================
int main(int argc, char* argv[])
{
    _setmode(_fileno(stdout), _O_U16TEXT);

    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportHook(AssertReportHook);

    const short nPort = (argc >= 2) ? (short)atoi(argv[1]) : kDefaultPort;

    wprintf(L"=== ExplorerTest - the TargetCore hub Explorer, end to end ===\n");
    wprintf(L"Hub  : %s   slots: %s\n", kHubAddr, kExpDomain);
    wprintf(L"Port : %d (127.0.0.1)\n\n", (int)nPort);
    fflush(stdout);

    g_hAssigned = CreateEvent(NULL, FALSE, FALSE, NULL);
    g_hAnswer   = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (!StartupP2Pmsg(16)) { LogAt(L"MAIN", L"FATAL: StartupP2Pmsg() failed"); return 1; }
    WSADATA oWsaData;
    WSAStartup(MAKEWORD(2, 2), &oWsaData);

    int nExit = 1;
    {
        P2PeerHub         oServer(kHubAddr);
        ExplorerClientHub oClient(kAnonAddr);

        //  ARMING: RequireAuth defaults to ON since ProductionPlan.md Stage 3
        //  step 8, and a hub that requires authentication it cannot enforce
        //  REFUSES TO ARM - SpawnHub() returns NULL rather than starting and
        //  then turning every peer away. This example provisions no identity
        //  and no allow-list, so it takes the documented one-line migration
        //  and says so out loud. NOT the posture to copy into a real hub.
        oServer.RequireAuth ( false );
        HANDLE hServerThread = oServer.SpawnHub();
        if (!hServerThread) { LogAt(L"SERVER", L"FATAL: SpawnHub() failed"); return 1; }
        LogAt(L"SERVER", L"hub thread started");

        // ---- Stand the Explorer up ---------------------------------------
        // Construct + spawn + register, in one documented call. Passing no
        // instance gets a stock P2PeerExplorer; passing one adopts it.
        P2PeerExpump* pExp = 0;
        try
        {
            pExp = P2PeerExpump_ACTIVATE(&oServer);
            if (!pExp)
            { LogAt(L"SERVER", L"FATAL: P2PeerExpump_ACTIVATE() failed"); return 1; }
            if (oServer.GetP2PeerExpump() != pExp)
            {
                LogAt(L"SERVER", L"FATAL: ACTIVATE left the hub with no expump - "
                                 L"nothing can route to the Explorer");
                return 1;
            }
        }
        catch (P2Pevent* pEVT)
        {
            pEVT->Advice(_T("ExplorerTest: could not stand up the Explorer"))->Cancel();
            LogAt(L"SERVER", L"FATAL: Explorer activation threw");
            return 1;
        }
        LogAt(L"SERVER", L"Explorer expump activated and registered with the hub");

        // The service connection belongs to the EXPUMP, not to the hub, and
        // its address is the SLOT DOMAIN -- the wildcard the Explorer fills in
        // one XCid at a time.
        P2PeerConWsa* pSvcCon = P2PeerConWsa::ServiceFactory(kExpDomain, nPort);
        if (!pSvcCon) { LogAt(L"SERVER", L"FATAL: ServiceFactory failed"); return 1; }
        if (pExp->PostP2PeerCon(pSvcCon))
        { LogAt(L"SERVER", L"FATAL: PostP2PeerCon() to the expump failed"); return 1; }
        LogAt(L"SERVER", L"Explorer service listening - slots handed out as CEX.XC%i");

        Sleep(500);     // let the listener bind before dialling

        // ---- The client dials, anonymously -------------------------------
        oClient.RequireAuth ( false );          // as above - unprovisioned example
        HANDLE hClientThread = oClient.SpawnHub();
        if (!hClientThread) { LogAt(L"CLIENT", L"FATAL: SpawnHub() failed"); return 1; }

        P2PeerConWsa* pCliCon = P2PeerConWsa::ClientFactory(kHubAddr, L"127.0.0.1", nPort);
        if (!pCliCon) { LogAt(L"CLIENT", L"FATAL: ClientFactory failed"); return 1; }
        oClient.PostP2PeerCon(pCliCon);
        LogAt(L"CLIENT", L"dialling - it declares no address of its own");

        // ---- 1. the Explorer must assign us a slot ------------------------
        if (WaitForSingleObject(g_hAssigned, 15000) != WAIT_OBJECT_0)
        {
            wprintf(L"\nRESULT: TIMEOUT - the Explorer never assigned an XCid.\n"
                    L"  Look above for \"No free ECid exploration slots available\"\n"
                    L"  or a \"Null P2Paddr expected\" rejection\n"
                    L"  (P2PeerExplorer.cpp:1320, :1302). If wsa_mesh / WsaMeshTest\n"
                    L"  is also red, fix that first - this needs loopback TCP.\n");
            nExit = 3;
        }
        else
        {
            wprintf(L"\n[MAIN] the Explorer assigned this peer '%s'\n", g_strMe.c_str());

            // ---- 2. QHub: ask once ---------------------------------------
            LogAt(L"MAIN", L"--- QHub: ask the hub what it is ---");
            oClient.SendCommand(L"QHub", FALSE, _T("ExplorerTest one-shot query"));
            const bool bAnswer1 = WaitForAnswers(1, 10000);
            const int  nNodes1  = g_nNodesLast;

            // ---- 3. QHub@Dsc: the qualifier the hub answer ignores --------
            LogAt(L"MAIN", L"--- QHub@Dsc: the same question, verbosely ---");
            oClient.SendCommand(L"QHub", TRUE, _T("ExplorerTest verbose query"));
            const bool bAnswer2 = WaitForAnswers(2, 10000);
            const int  nNodes2  = g_nNodesLast;

            // ---- 4. RHub: answer AND register as a standing sink ----------
            LogAt(L"MAIN", L"--- RHub: register as a hub-status sink ---");
            oClient.SendCommand(L"RHub", FALSE, _T("ExplorerTest sink registration"));
            const bool bAnswer3 = WaitForAnswers(3, 10000);

            // ---- 5. DHub: de-register; nothing should come back -----------
            LogAt(L"MAIN", L"--- DHub: de-register (no answer is the correct answer) ---");
            oClient.SendCommand(L"DHub", FALSE, _T("ExplorerTest de-registration"));
            Sleep(1500);
            const bool bQuietAfterDHub = (InterlockedExchangeAdd(&g_nAnswers, 0) == 3);

            // ---- Verdict --------------------------------------------------
            wprintf(L"\n--- what happened ---\n");
            wprintf(L"  QHub answered ............ %s\n", bAnswer1 ? L"yes" : L"NO");
            wprintf(L"  QHub@Dsc answered ........ %s\n", bAnswer2 ? L"yes" : L"NO");
            wprintf(L"  RHub answered ............ %s\n", bAnswer3 ? L"yes" : L"NO");
            wprintf(L"  DHub stayed quiet ........ %s\n", bQuietAfterDHub ? L"yes" : L"NO");
            wprintf(L"  directory carried Machine  %s\n", g_bSawMachine ? L"yes" : L"NO");
            wprintf(L"  directory carried Executable %s\n", g_bSawExecutable ? L"yes" : L"NO");
            wprintf(L"  nodes: QHub=%d  QHub@Dsc=%d%s\n", nNodes1, nNodes2,
                    (nNodes1 == nNodes2)
                      ? L"  (identical, as expected - @Dsc is parsed and then unused)"
                      : L"  (DIFFERENT - QueryP2PmsgExp_Hub has learnt to use bVerbose)");

            if (bAnswer1 && bAnswer2 && bAnswer3 && bQuietAfterDHub &&
                g_bSawMachine && g_bSawExecutable)
            {
                wprintf(L"\nRESULT: SUCCESS - the hub was asked what it is, three times,\n"
                        L"  and said so; the de-registration was honoured.\n");
                nExit = 0;
            }
            else
            {
                wprintf(L"\nRESULT: TIMEOUT/INCOMPLETE - see the table above.\n");
                nExit = 3;
            }
        }

        // ---- Shutdown ----------------------------------------------------
        // Order matters. Stop the expump in ITS OWN context first: the hub's
        // CloseHub -> DropP2PeerExpump then DELETES it, and ~P2PeerExplorer
        // used to throw out of exactly that delete because RegisterP2Pexpump /
        // CloseP2Pexpump are keyed on GetCurrentThreadId() and are legal only
        // from the expump's own thread.
        LogAt(L"MAIN", L"shutdown begin");
        try { oClient.CloseHub(); } catch (P2Pevent* pEVT) { pEVT->Cancel(); }
        WaitForSingleObject(hClientThread, 3000);
        CloseHandle(hClientThread);

        try { if (pExp) pExp->CloseExpump(0); }
        catch (P2Pevent* pEVT)
        { pEVT->Advice(_T("ExplorerTest: CloseExpump"))->Cancel(); }

        try { oServer.CloseHub(); } catch (P2Pevent* pEVT) { pEVT->Cancel(); }
        WaitForSingleObject(hServerThread, 3000);
        CloseHandle(hServerThread);

        if (oServer.GetP2PeerExpump())
            LogAt(L"MAIN", L"WARNING: the hub still holds an expump pointer after CloseHub()");
    }

    CleanupP2Pmsg();
    if (g_hAssigned) { CloseHandle(g_hAssigned); g_hAssigned = NULL; }
    if (g_hAnswer)   { CloseHandle(g_hAnswer);   g_hAnswer   = NULL; }
    WSACleanup();

    wprintf(L"Done (exit=%d).\n", nExit);
    fflush(stdout);
    return nExit;
}

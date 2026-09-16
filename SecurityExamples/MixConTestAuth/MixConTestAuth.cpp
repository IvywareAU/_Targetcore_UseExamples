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
// MixConTestAuth.cpp
//
// TEST 2b -- MixConTest with the SECURITY DEFAULTS LEFT ON.
//
//            Same question as MixConTest ("can ONE P2PeerHub hold a
//            P2PeerConWsa and a P2PeerConPipe at the same time?"), asked of a
//            hub that is actually PROVISIONED rather than one that opted out:
//
//                RequireAuth(true)   -- the default, and the arming gate
//                RequireSeal(true)   -- the default, and the outbound gate
//                RequireRevocation   -- left at its default (true) as well
//
// ---------------------------------------------------------------------------
// Why this file exists alongside MixConTest
// ---------------------------------------------------------------------------
// MixConTest takes the documented one-line migration on each hub:
//
//      oHub.RequireAuth (false);      // Stage 3 step 8
//      oHub.RequireSeal (false);      // Stage 3 step 20
//
// That is the correct thing for a binary whose subject is TRANSPORTS -- and it
// means the mixed-transport claim has only ever been demonstrated on the ONE
// posture nobody deploys. Two things go untested by it:
//
//   * the login handshake and the per-connection session cypher, which ride on
//     RequireAuth and are transport-agnostic ONLY IF the pipe path and the
//     socket path both carry them. That is an assertion until something makes
//     both do it in the same process, in the same run;
//   * the arming gate itself, which is the part an operator meets first. A
//     hub that is provisioned correctly must ARM; the interesting failure is
//     not "auth broke", it is "auth was never on".
//
// So this binary changes exactly one thing about MixConTest and nothing else:
// it provisions the three hubs instead of opting them out. Same three-hub
// topology, same distinct-identity rule, same request/response proof.
//
//        HubB (WSA service) <=== TCP  127.0.0.1:7801 ===  HubA  (WSA client)
//        HubC (Pipe service) <== \\.\pipe\MixConAuthProbe = |    (Pipe client)
//                                                           `--- THE MIXED HUB
//
// (Addresses, port and pipe name all differ from MixConTest's so the two can
// run at the same time without fighting over either endpoint.)
//
// ---------------------------------------------------------------------------
// What provisioning actually is, in the order the arming gate checks it
// ---------------------------------------------------------------------------
// P2PeerHub::SpawnHub() calls AuthArmOrRefuse() BEFORE the pump thread exists
// (Targetcore/P2PeerHub.cpp:187). AuthArm() then refuses ONE REASON AT A TIME,
// in this order, so satisfying one moves the refusal to the next rather than
// clearing it:
//
//   ArmNoIdentity          SetIdentity()/ProvisionAuth() never succeeded
//   ArmNoAllowList         SetAllowList() never called
//   ArmAllowUnusable       configured, and the last load FAILED
//   ArmEmptyAllow          loads, parses, and names nobody
//   ArmNoRevocation        no revocation list, and no RequireRevocation(false)
//   ArmRevocationUnusable  configured and will not load -> refuses EVERY peer
//
// Which is why this file does all four things, per hub:
//
//   1. ProvisionAuth()    creates <stem>.key if absent, loads it if not, and
//                         writes <stem>.key.pub -- the publishable half.
//   2. SetAgreementKey()  the SEPARATE ECDH key others seal TO. Not the same
//                         key as the identity and deliberately not able to be:
//                         different container magic, different DPAPI entropy.
//                         Without it a hub that requires sealing still ARMS,
//                         but warns that it could not open a body sent to it.
//   3. SetAllowList()     who this hub will believe. HubA lists HubB and HubC;
//                         HubB and HubC each list HubA. Three columns, so the
//                         peers can also be sealed to.
//   4. SetRevocationList()  a revocation POSITION. The file must EXIST -- a
//                         configured list that will not load fails CLOSED and
//                         refuses every peer. An all-comments file is a valid
//                         "nothing is revoked yet". The alternative position,
//                         RequireRevocation(false), is one line and is the
//                         right answer for a closed tree; it is shown below
//                         and deliberately not taken.
//
// The allow-list is regenerated on every run and the KEYS ARE NOT. That is the
// same asymmetry the library has: ProvisionAuth run twice FINDS the key rather
// than replacing it (a first-run helper that rotated on restart would change
// every hub's identity behind the operator's back), while AppendAllowList does
// not de-duplicate, so an allow-list appended to on every run would grow a
// line per launch. Deleting and rebuilding it is cheap and idempotent.
//
// ---------------------------------------------------------------------------
// What RequireSeal(true) does and does NOT do here
// ---------------------------------------------------------------------------
// Nothing in this test is ever sealed, and that is the correct outcome rather
// than a gap. P2PeerCon::SealAppMsgOutbound (Targetcore/P2PeerCon.cpp:2773)
// returns early when the link is the LAST HOP -- `if (m_oThatP2Paddr ==
// strScope) return true;` -- and every message here is single-hop: HubA->HubB
// rides the connection whose far end IS HubB. Sealing engages when a message
// must cross an INTERMEDIATE hub, and this topology has none.
//
// What RequireSeal(true) buys, then, is the posture and not a behaviour:
//   * with the agreement keys provisioned, CanOpen() is true and the arm-time
//     warning ("requires sealing and holds no agreement key") does not fire;
//   * the allow-lists carry the third column, so if a fourth hub were added
//     and traffic relayed, the seal would succeed rather than the send being
//     REFUSED. It is refuse, never downgrade -- there is no path that quietly
//     sends a relayed body in clear.
// RequireSealBroadcast is likewise left at its default (true) and is likewise
// inert: HasScope() is the broadcast test, TMsg_Scp is stamped only by the
// On_P2PeerBCast/On_P2PeerUCast fan-out, and these are P2Pmsg_BCast-NAMED
// unicasts, which nothing stamps.
//
// ---------------------------------------------------------------------------
// Verdict via EXIT CODE
// ---------------------------------------------------------------------------
//   0 = PASS : all three hubs ARMED with auth required (AuthArm() == ArmOk,
//              not ArmNotRequired), HubA completed On_ConLoginAck on BOTH the
//              WSA and the pipe connection, and received a response over BOTH.
//   1 = setup failure (startup / provisioning / factory / SpawnHub).
//   2 = an MFC/CRT assertion fired.
//   3 = a post that should have succeeded returned FALSE, a transport did not
//       complete login within the timeout, or a response did not come back.
//   4 = a hub refused to ARM. Distinct from 1 on purpose: it is the one
//       failure whose cause is a FILE and not the code, and the log line names
//       both the reason and the file.

#include "stdafx.h"
#include "MixConTestAuth.h"

#include <crtdbg.h>

#include "P2Pwin32.h"
#include "P2PeerHub.h"
#include "P2PeerConWsa.h"
#include "P2PeerConPipe.h"
#include "P2PeerMsg.h"
#include "Msgexception.h"

#include "P2PCngCrypto.h"       // EcdsaP256, kEcdsaPubLen, kEcdhPubLen
#include "P2PIdentityStore.h"   // key files, allow-list, revocation list
#include "P2PAuthLogin.h"       // p2pauth::ArmResult, AuthArmText

#include <string>
#include <string.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// MFC requires exactly one CWinApp instance per executable
CWinApp theApp;

// -------------------------------------------------------------------------
// Configuration
// -------------------------------------------------------------------------
static const short      kPort     = 7801;
static LPCTSTR          kPipeName = _T("\\\\.\\pipe\\MixConAuthProbe");

static const P2PaddrSTR kHubA = L"MixConTestAuth.HubA";   // the MIXED hub
static const P2PaddrSTR kHubB = L"MixConTestAuth.HubB";   // WSA  peer of HubA
static const P2PaddrSTR kHubC = L"MixConTestAuth.HubC";   // Pipe peer of HubA

// The allow-list keys peers by the NARROW form of the same address, and the
// comparison is exact - no case folding, no normalisation, because a trust
// decision should not depend on locale rules. Kept beside the wide constants
// so the two cannot drift.
static const char* kHubA_A = "MixConTestAuth.HubA";
static const char* kHubB_A = "MixConTestAuth.HubB";
static const char* kHubC_A = "MixConTestAuth.HubC";

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
// MixConAuthHub
//   One instrumented hub subclass, reused for all three hubs. Identical to
//   MixConTest's MixConHub - the difference between the two binaries is the
//   PROVISIONING in main(), not the hub behaviour.
// =========================================================================
class MixConAuthHub : public P2PeerHub
{
public:
    MixConAuthHub(P2PaddrSTR strAddr, LPCWSTR lpszLabel)
        : P2PeerHub(strAddr), m_strAddr(strAddr), m_strLabel(lpszLabel) {}
    virtual ~MixConAuthHub() {}

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
            LogAt(m_strLabel, L"  -> WSA (TCP) connection login acked (signed + cyphered)");
            // Transport is live: send an application request over the socket.
            PostRequest(kHubB, L"Hello HubB over the WSA socket");
        }
        else if (addr && wcscmp(addr, kHubC) == 0)     // HubA's pipe con
        {
            InterlockedExchange(&g_nPipeAck, 1);
            LogAt(m_strLabel, L"  -> PIPE connection login acked (signed + cyphered)");
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
// Provisioning
//   Everything below runs BEFORE SpawnHub(). None of it is valid afterwards
//   except ReloadAllowList()/ReloadRevocationList(), which are the two calls
//   that take the hub critical section and are meant for a running hub.
// =========================================================================

// Where the key material lives. Beside the executable rather than at a fixed
// absolute path, so a clone of the tree in another directory just works, and
// so a stale key from a different build tree is never picked up silently.
static std::string g_strDir;

static bool ResolveKeyDir()
{
    char szExe[MAX_PATH + 1] = { 0 };
    DWORD n = GetModuleFileNameA(NULL, szExe, MAX_PATH);
    if (n == 0 || n > MAX_PATH) return false;

    std::string s(szExe);
    size_t iSlash = s.find_last_of("\\/");
    if (iSlash == std::string::npos) return false;

    g_strDir = s.substr(0, iSlash + 1) + "p2p\\";
    CreateDirectoryA(g_strDir.c_str(), NULL);
    return true;
}

// The public halves of one hub, as they go into the OTHER hubs' allow-lists.
struct HubKeys
{
    unsigned char aId    [p2pcng::kEcdsaPubLen];   // ECDSA - proves identity
    unsigned char aAgree [p2pcng::kEcdhPubLen];    // ECDH  - what others seal to
    bool          bAgree;
    char          szFingerprint[p2pcng::kIdFingerprintLen];
};

//  Give one hub an identity and an agreement key, and hand back the two
//  publishable points.
//  NOTES: ProvisionAuth() is the FIRST-RUN helper and it is idempotent by
//         design - run twice, the second run FINDS the key rather than
//         replacing it. It writes <stem>.key.pub, the file whose single line
//         an operator would paste into a peer's allow-list; here that step is
//         done in-process, which is the only difference from a real
//         deployment.
//       : It deliberately does NOT create the allow-list. Who to trust is the
//         half of provisioning that is not mechanical, and a library that
//         silently created an empty one would be answering it with "nobody" -
//         which refuses every peer, and arms as ArmEmptyAllow.
//       : The identity and agreement files are NOT interchangeable: different
//         container magic and different DPAPI entropy, so a swapped or renamed
//         file fails loudly instead of quietly making a hub sign with the key
//         it agrees with.
static bool ProvisionHub(P2PeerHub& rHub, const char* pszStem, HubKeys& rOut)
{
    memset(&rOut, 0, sizeof(rOut));

    const std::string strKey   = g_strDir + pszStem + ".key";
    const std::string strAgree = g_strDir + pszStem + ".agree";
    const std::string strPub   = strKey + ".pub";

    bool bCreated = false;
    p2pcng::IdResult e = rHub.ProvisionAuth(strKey.c_str(),
                                            rOut.szFingerprint,
                                            sizeof(rOut.szFingerprint),
                                            &bCreated);
    if (e != p2pcng::IdOk)
    {
        wprintf(L"  FATAL: ProvisionAuth(%hs) -> %hs\n",
                strKey.c_str(), p2pcng::IdResultText(e));
        return false;
    }

    //  The ECDH key others seal TO. Without it the hub still arms - there is
    //  deliberately no ArmNoAgreement, because a relay legitimately holds no
    //  keys - but it warns at arm time that it could not open a body sealed
    //  to it, and it could not be a sealing destination.
    e = rHub.SetAgreementKey(strAgree.c_str(), /*create if absent*/ true);
    if (e != p2pcng::IdOk)
    {
        wprintf(L"  FATAL: SetAgreementKey(%hs) -> %hs\n",
                strAgree.c_str(), p2pcng::IdResultText(e));
        return false;
    }

    //  Read back the publishable identity point from the .pub ProvisionAuth
    //  just wrote. LoadPublicKey accepts either a bare hex line or a full
    //  allow-list line, so the same file serves both purposes.
    p2pcng::EcdsaP256 oPub;
    e = p2pcng::LoadPublicKey(strPub.c_str(), oPub);
    if (e != p2pcng::IdOk)
    {
        wprintf(L"  FATAL: LoadPublicKey(%hs) -> %hs\n",
                strPub.c_str(), p2pcng::IdResultText(e));
        return false;
    }
    if (!oPub.ExportPublic(rOut.aId))
    {
        wprintf(L"  FATAL: ExportPublic(%hs) failed\n", pszStem);
        return false;
    }

    //  The agreement point comes straight off the hub - there is no
    //  provisioning helper for it, and none is needed.
    rOut.bAgree = rHub.GetAgreementPublic(rOut.aAgree);

    wprintf(L"  %-5hs  identity=%-7hs  agreement=%-3hs  fp=%hs\n",
            pszStem,
            bCreated ? "created" : "found",
            rOut.bAgree ? "yes" : "NO",
            rOut.szFingerprint);
    fflush(stdout);
    return true;
}

//  Rebuild one hub's allow-list from scratch, then hand it to the hub.
//  NOTES: DELETED FIRST, ON PURPOSE. AppendAllowList does not de-duplicate -
//         LoadAllowList/FindAllowed take the first match, so a duplicate line
//         is dead weight rather than a hazard - but a file appended to on
//         every launch grows a line per run for ever. The keys persist; the
//         list is derived from them, so rebuilding it is free and idempotent.
//       : Three columns. The optional third is the peer's agreement point,
//         and it is what makes that peer a legal SEALING DESTINATION. A
//         two-column entry still logs in; asking to seal to it returns
//         SealErrNoAgreement, and there is deliberately no fallback that
//         sends the body in clear because the directory was incomplete.
//       : A line that does not parse fails the WHOLE load (ArmAllowUnusable),
//         rather than being skipped - a half-read allow-list silently denies
//         peers that should be allowed.
static bool WriteAllowList(P2PeerHub& rHub, const char* pszStem,
                           const char* pszPeer1, const HubKeys& rK1,
                           const char* pszPeer2, const HubKeys& rK2)
{
    const std::string strAllow = g_strDir + pszStem + ".allow";
    DeleteFileA(strAllow.c_str());

    p2pcng::IdResult e = p2pcng::AppendAllowList(
                             strAllow.c_str(), pszPeer1, rK1.aId,
                             rK1.bAgree ? rK1.aAgree : nullptr);
    if (e == p2pcng::IdOk && pszPeer2)
        e = p2pcng::AppendAllowList(
                strAllow.c_str(), pszPeer2, rK2.aId,
                rK2.bAgree ? rK2.aAgree : nullptr);
    if (e != p2pcng::IdOk)
    {
        wprintf(L"  FATAL: AppendAllowList(%hs) -> %hs\n",
                strAllow.c_str(), p2pcng::IdResultText(e));
        return false;
    }

    e = rHub.SetAllowList(strAllow.c_str());
    if (e != p2pcng::IdOk)
    {
        wprintf(L"  FATAL: SetAllowList(%hs) -> %hs\n",
                strAllow.c_str(), p2pcng::IdResultText(e));
        return false;
    }

    wprintf(L"  %-5hs  allow-list -> %hs (%hs%hs%hs)\n",
            pszStem, strAllow.c_str(),
            pszPeer1, pszPeer2 ? ", " : "", pszPeer2 ? pszPeer2 : "");
    fflush(stdout);
    return true;
}

//  Make sure the shared revocation list EXISTS, so naming it is a position
//  rather than a self-inflicted outage.
//  NOTES: FAIL CLOSED. Once a revocation list is configured, a load that
//         fails - deleted, unreadable, one bad line - does NOT fall back to
//         "nothing is revoked": every verification returns AuthErrRevoked and
//         the hub refuses to arm with ArmRevocationUnusable. A file of nothing
//         but comments is the honest empty state and loads cleanly.
//       : Keyed on the full 64-byte point, NOT the fingerprint. Fingerprint()
//         says in its own contract that it is never an identifier the code
//         trusts, and refusing a peer is exactly the code trusting one; the
//         fingerprint belongs in the trailing comment, where AppendRevocation-
//         List puts it.
static bool EnsureRevocationList(const std::string& strPath)
{
    if (GetFileAttributesA(strPath.c_str()) != INVALID_FILE_ATTRIBUTES)
        return true;

    FILE* pf = nullptr;
    if (fopen_s(&pf, strPath.c_str(), "wb") != 0 || !pf)
    {
        wprintf(L"  FATAL: cannot create %hs\n", strPath.c_str());
        return false;
    }
    fputs("# MixConTestAuth revocation list.\n"
          "#\n"
          "# One revoked PUBLIC POINT per line, 128 hex characters - the same\n"
          "# column the allow-list already carries - with an optional epoch:\n"
          "#\n"
          "#     <128 hex point> [<epoch seconds>]   # why\n"
          "#\n"
          "# One file covers identity AND agreement points: both are 64 raw\n"
          "# bytes, and with two files an operator can revoke a compromised\n"
          "# peer for login and forget sealing. Revocation is absolute - the\n"
          "# epoch column records WHEN for the operator and is never compared\n"
          "# against the clock. There is no removal API; un-revoking is\n"
          "# deleting a line by hand, which is deliberate friction.\n"
          "#\n"
          "# Nothing revoked yet.\n", pf);
    fclose(pf);
    return true;
}

//  Report the arming decision in our own words before SpawnHub reports it in
//  the library's. Both come from p2pauth::AuthArmText, so they cannot drift.
//  ArmOk is the pass here - ArmNotRequired would mean auth is OFF, which is
//  exactly the state this binary exists to not be in.
static bool CheckArmed(P2PeerHub& rHub, const char* pszStem)
{
    p2pauth::ArmResult e = rHub.AuthArm();
    const char* pszWhy = p2pauth::AuthArmText(e);

    if (e == p2pauth::ArmOk)
    {
        wprintf(L"  %-5hs  AuthArm() -> ArmOk (%hs)\n", pszStem, pszWhy);
        fflush(stdout);
        return true;
    }

    //  WHICH FILE THE REFUSAL NAMES FOLLOWS THE REASON. Sending an operator
    //  to the allow-list when the revocation list is the problem is worse
    //  than naming nothing - the two live in the same directory under similar
    //  names. This is the same choice AuthArmOrRefuse makes internally.
    const char* pszPath = (e == p2pauth::ArmNoRevocation ||
                           e == p2pauth::ArmRevocationUnusable)
                        ? rHub.AuthRevocationListPath()
                        : rHub.AuthAllowListPath();

    wprintf(L"  %-5hs  AuthArm() -> %hs\n", pszStem, pszWhy);
    if (pszPath)
        wprintf(L"         file: %hs\n", pszPath);
    if (e == p2pauth::ArmNotRequired)
        wprintf(L"         ArmNotRequired means auth is OFF - this binary "
                L"requires it to be ON.\n");
    fflush(stdout);
    return false;
}


// =========================================================================
// main
// =========================================================================
int main()
{
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportHook(AssertReportHook);

    wprintf(L"=== MixConTestAuth -- mixed transports with the security "
            L"defaults LEFT ON ===\n");
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
    MixConAuthHub oHubA(kHubA, L"HubA");  // mixed endpoint (WSA + pipe client)
    MixConAuthHub oHubB(kHubB, L"HubB");  // WSA  service (accepts HubA)
    MixConAuthHub oHubC(kHubC, L"HubC");  // Pipe service (accepts HubA)

    // =====================================================================
    // PROVISIONING - all of it BEFORE SpawnHub()
    // =====================================================================
    if (!ResolveKeyDir())
    { wprintf(L"FATAL: could not resolve the key directory.\n"); return 1; }

    wprintf(L"-- keys (%hs)\n", g_strDir.c_str());

    HubKeys oKeyA, oKeyB, oKeyC;
    if (!ProvisionHub(oHubA, "HubA", oKeyA) ||
        !ProvisionHub(oHubB, "HubB", oKeyB) ||
        !ProvisionHub(oHubC, "HubC", oKeyC))
    { wprintf(L"FATAL: provisioning failed.\n"); return 1; }

    // Who trusts whom. HubA is the connector on both transports, so it must
    // be able to verify BOTH peers' login acks; each service hub verifies
    // HubA's login. A peer that is not listed is refused as AuthErrUnknownPeer
    // - an allow-list entry is not a pattern, and "MixConTestAuth.*" in that
    // column admits nothing.
    wprintf(L"\n-- allow-lists\n");
    if (!WriteAllowList(oHubA, "HubA", kHubB_A, oKeyB, kHubC_A, oKeyC) ||
        !WriteAllowList(oHubB, "HubB", kHubA_A, oKeyA, nullptr,  oKeyA) ||
        !WriteAllowList(oHubC, "HubC", kHubA_A, oKeyA, nullptr,  oKeyA))
    { wprintf(L"FATAL: allow-list setup failed.\n"); return 1; }

    // A revocation POSITION is required before a hub that requires auth will
    // arm. Two answers, both one line and both honest; this takes the first.
    //
    //     SetRevocationList(path)     name a list - shown below
    //     RequireRevocation(false)    a closed tree whose keys are
    //                                 provisioned once and never withdrawn
    //
    // The second does NOT turn revocation off: a list configured anyway is
    // still loaded, still enforced and still fails closed. It says only that
    // the ABSENCE of one is deliberate. What is no longer reachable is a hub
    // that cannot withdraw a compromised key and never said that was the
    // intention.
    wprintf(L"\n-- revocation\n");
    const std::string strRevoke = g_strDir + "peers.revoked";
    if (!EnsureRevocationList(strRevoke))
    { wprintf(L"FATAL: revocation list setup failed.\n"); return 1; }

    if (oHubA.SetRevocationList(strRevoke.c_str()) != p2pcng::IdOk ||
        oHubB.SetRevocationList(strRevoke.c_str()) != p2pcng::IdOk ||
        oHubC.SetRevocationList(strRevoke.c_str()) != p2pcng::IdOk)
    { wprintf(L"FATAL: SetRevocationList(%hs) failed.\n", strRevoke.c_str());
      return 1; }
    wprintf(L"  list -> %hs (loads, nothing revoked)\n", strRevoke.c_str());

    // The defaults, said out loud. None of these three lines changes anything
    // - RequireAuth, RequireSeal and RequireRevocation are all true already -
    // and they are here because this binary's subject is the posture, so the
    // posture should be readable in the source rather than inferred from an
    // absence.
    oHubA.RequireAuth(true);   oHubA.RequireSeal(true);
    oHubB.RequireAuth(true);   oHubB.RequireSeal(true);
    oHubC.RequireAuth(true);   oHubC.RequireSeal(true);

    // ---- Pre-flight: report the arming decision ourselves ---------------
    wprintf(L"\n-- arming\n");
    if (!CheckArmed(oHubA, "HubA") ||
        !CheckArmed(oHubB, "HubB") ||
        !CheckArmed(oHubC, "HubC"))
    {
        LogAt(L"MAIN", L"FAIL: a hub would not arm -- see the reason above");
        return 4;
    }

    HANDLE hA = oHubA.SpawnHub();
    HANDLE hB = oHubB.SpawnHub();
    HANDLE hC = oHubC.SpawnHub();
    if (!hA || !hB || !hC)
    { wprintf(L"FATAL: SpawnHub() failed.\n"); return 1; }
    LogAt(L"MAIN", L"three PROVISIONED hubs spawned (auth required, seal required)");

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
    // This is the crux: one hub, two different transports, at the same time -
    // and now with a signed, cyphered login on each.
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
            LogAt(L"MAIN", L"HubA completed an AUTHENTICATED login on BOTH transports");

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
            wprintf(L"  A login refused after the transport connected is an AUTH "
                    L"failure, not a transport one:\n"
                    L"  AuthErrUnknownPeer (peer not in the allow-list), "
                    L"AuthErrSkew (clock outside +/-300s),\n"
                    L"  AuthErrRevoked (listed, or the revocation list would not "
                    L"load - it fails CLOSED).\n");
            nExit = 3;
        }
    }

    // ---- The posture, read back rather than asserted -------------------
    // TryReadPosture never blocks: it TRIES the hub critical section and gives
    // up rather than waiting, because a snapshot must never be able to hang
    // the hub it describes. "unreadable" here means busy, not broken.
    {
        P2PeerHub::Posture oP;
        if (oHubA.TryReadPosture(oP))
            wprintf(L"\n  HubA posture: auth=%d sign=%d arm=%hs seal=%d "
                    L"open=%d bcast=%d revoc(cfg=%d ok=%d)\n",
                    (int)oP.bAuthRequired, (int)oP.bAuthCanSign,
                    p2pauth::AuthArmText((p2pauth::ArmResult)oP.nAuthArm),
                    (int)oP.bSealRequired, (int)oP.bSealCanOpen,
                    (int)oP.bSealBcast,
                    (int)oP.bRevocConfigured, (int)oP.bRevocOk);
        else
            wprintf(L"\n  HubA posture: UNREADABLE (hub busy - ask again)\n");
        fflush(stdout);
    }

    if (nExit == 0)
        LogAt(L"MAIN", L"VERDICT: PASS -- three PROVISIONED hubs armed with auth "
                       L"required; one hub (HubA) holds a live P2PeerConWsa AND a "
                       L"live P2PeerConPipe, both authenticated, and exchanged a "
                       L"request/response over each");
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

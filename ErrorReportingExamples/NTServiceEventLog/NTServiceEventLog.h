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
//  NTServiceEventLog - a P2PeerHub hosted as a Windows service, reporting to
//                      the Windows event log
//  NOTES: Refer README.md in this directory for what this demonstrates and how
//         to run it.
//
#pragma once

#include "P2Peer.h"
#include "P2PeerHub.h"
#include "P2PeerMsg.h"
#include "P2PeerService.h"
#include "Msgexception.h"

///////////////////////////////////////////////////////////////////////
//  AppEventLog - the application's own event log source
//  NOTES: The library already writes ITS diagnostics to the log for a hosted
//         hub: P2PeerService::Run() installs a P2Pevent text sink in service
//         mode, under the service's own name as the source.  This class is for
//         the events that are the APPLICATION's rather than the library's -
//         "the configuration named a port I cannot use", "a peer was refused"
//         - which no library can raise on your behalf because it does not know
//         they are events.
//       : It reuses Targetcore's committed message catalogue (TargetcoreEvt.h,
//         built from TargetcoreEvt.mc) rather than carrying one of its own.
//         The catalogue's entries take two insertion strings and render them
//         verbatim, which is all an application needs to put its own text in
//         the log.  If you want your own event IDs to alert on, write your own
//         .mc and point EventMessageFile at your own module instead - the
//         mechanism here is identical, only the numbers change.
//
class AppEventLog
{
    // Constructors and destructor
    public:
        AppEventLog ( );
       ~AppEventLog ( );
    private:
        AppEventLog ( const AppEventLog& );
      AppEventLog& operator = ( const AppEventLog& );

    // Life cycle
    public:
      bool
        Open  ( LPCWSTR lpszSource );
      void
        Close ( );
      bool
        IsOpen ( ) const { return m_hLog != 0; }

    // Reporting
    public:
      void
        Write  ( P2Pevent_e eClass, LPCWSTR lpszOrigin, LPCWSTR lpszText );
      void
        Report ( P2Pevent_e eClass, LPCWSTR lpszOrigin
               , LPCWSTR lpszFormat, ... );

    // Installation - HKLM, so elevated only
    public:
      static bool
        Register   ( LPCWSTR lpszSource );
      static bool
        Unregister ( LPCWSTR lpszSource );
      static bool
        MessageFilePath ( CStringW& strPath );

    // Attributes
    private:
      HANDLE m_hLog;
};

///////////////////////////////////////////////////////////////////////
//  ReportingHub - the hub this service hosts
//  NOTES: Both overrides below run ON A PUMP THREAD, which is the whole point
//         of the example.  A pump is the thread the hub's teardown waits for,
//         so anything that blocks one blocks CloseHub() with it - and a modal
//         dialog blocks the thread that raised it until somebody clicks OK.
//         In a service nobody can. Refer README.md, "Why a dialog is a hang".
//
class ReportingHub : public P2PeerHub
{
    // Constructors and destructor
    public:
        ReportingHub ( P2PaddrSTR strP2PaddrHub, AppEventLog *pLog );
      virtual
       ~ReportingHub ( );

    // P2PeerHub overrides
    public:
      virtual void
        RunHub ( ) override;

    // Message handlers - pump thread
    // NOTES: No DECLARE_P2PeerMsg_MAP here on purpose.  These are virtual in
    //        P2PeerHub and the base class's map dispatches through the vtable,
    //        so an override is reached without this class declaring a map of
    //        its own.  A subclass only needs its own map when it handles a
    //        message the base does not.
    protected:
      virtual msgRESULT
        On_P2PeerBCast ( P2PeerMsg *pMsg ) override;
      virtual msgRESULT
        On_P2PeerError ( P2PeerMsg *pMsg ) override;

    // Attributes
    private:
      AppEventLog *m_pLog;               // Not owned
      long         m_cBCast;
};

///////////////////////////////////////////////////////////////////////
//  ReportingService - the SCM container
//
class ReportingService : public P2PeerService
{
    // Constructors and destructor
    public:
        ReportingService ( );
      virtual
       ~ReportingService ( );

    // P2PeerService overrides
    public:
      virtual DWORD
        Init ( DWORD argc, LPTSTR *argv ) override;
      virtual void
        OnStop ( ) override;
      virtual void
        PostInstall ( ) override;
      virtual void
        PostUnInstall ( ) override;

    // Reporting
    public:
      AppEventLog&
        Log ( ) { return m_oLog; }

    // Attributes
    private:
      AppEventLog m_oLog;
};

//  The event log source this example registers for its OWN events.  Separate
//  from the service name, which P2PeerService registers for the LIBRARY's
//  diagnostics, so the two are distinguishable in the Event Viewer.
#define APP_EVENT_SOURCE   L"P2PmsgErrorReportingExample"
#define APP_SERVICE_NAME   L"P2PmsgEventLogExample"
#define APP_HUB_ADDRESS    L"Example.EventLog"

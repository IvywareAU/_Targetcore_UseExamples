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
// TargetComInterop.cs
//
// The type library, by hand.
//
// A C++ client gets IP2PHubCom and friends from MIDL's TargetCom_h.h. A .NET
// client normally gets them from TlbImp / "Add Reference", which produces an
// *interop assembly* -- a managed DLL of nothing but [ComImport] interface
// declarations. This file IS that interop assembly, written out in source so
// the tree has no generated inputs and no build step beyond csc.
//
// It matters more than it sounds. The C++ tree's README says a PowerShell or
// VBScript client cannot sink _IP2PHubEvents because ".NET needs an interop
// assembly for the coclass". That is true, and this file is the counter-example
// rather than the exception: with the dispinterface declared, a plain managed
// class implements it, the CLR builds the CCW, and the connection point advises
// it exactly as it would a hand-written C++ IDispatch. The C# tree therefore
// gets the events that the two scripting clients could not.
//
// The declarations are a mechanical transcription of TargetCom.idl. Two things
// are load-bearing:
//
//   * VTABLE ORDER. InterfaceIsDual means "IUnknown + IDispatch, then my members
//     in declaration order". Members must appear here in the order of their
//     IDL id()s -- including the propget properties, which occupy a slot each.
//   * MARSHALLING. Everything is annotated even where the default would do, so
//     the IDL <-> C# mapping can be read off the page:
//
//         BSTR                     ->  [MarshalAs(UnmanagedType.BStr)] string
//         VARIANT                  ->  [MarshalAs(UnmanagedType.Struct)] object
//         SAFEARRAY(VT_UI1)        ->  byte[]  (inside that object)
//         VARIANT_BOOL             ->  [MarshalAs(UnmanagedType.VariantBool)] bool
//         LONG                     ->  int
//         [out, retval] X *pVal    ->  the C# return value
//
// HRESULTs are NOT PreserveSig here. A failed call raises COMException and
// ex.HResult carries the facade's own code out unchanged -- which is the shape
// the C++ tree's README predicted for a C# caller, and the shape Com.Hub turns
// back into an int for harnesses that want to inspect it.
//
// THAT SHAPE HAS A BLIND SPOT, and ABI 4 walked straight into it. It works
// because every interesting HRESULT here is a FAILURE code. P2PF_S_UNRELATED_LINK
// (0x0004020C) is not: severity 0, meaning "armed, but these two addresses are
// neither ancestor nor descendant, so nothing can be ROUTED through the edge".
// The marshaller **discards** a successful HRESULT -- with a `void` signature
// there is no exception and no return value -- so a .NET caller cannot tell
// S_OK from S_UNRELATED_LINK. Every X.Server/X.Client pair in this tree gets
// S_UNRELATED_LINK from the kernel and S_OK from the marshaller.
//
// These declarations stay `void` anyway, because this file's whole premise is
// that it is what TlbImp and "Add Reference" would have generated. Making the
// arming pair `[PreserveSig] int` here would make the code visible and make the
// file unrepresentative of what a real .NET consumer gets -- so the honest
// answer is: **that information does not reach a normal .NET client at all.**
//
// It is not lost, though. The facade raises one OnError alongside the code, and
// the event carries the full sentence; every harness here logs it. So the C#
// tree learns the same fact through the event that the C++ trees read off the
// return value. If you need the value itself, declare the two members
// [PreserveSig] int in your own copy of this file -- but know that you have
// then left what the type library alone can give you.
//
// SINCE THEN, the type library grew a proper answer: the READ SIDE, dispids
// 9-13, declared at the end of IP2PHubCom below. The arming verbs still say
// nothing on success -- that is what automation does to a success HRESULT -- but
// the hub can now be asked afterwards, from any tier, whether an edge is a
// sibling one. That is a better shape than either the event or PreserveSig: it
// is a code rather than a sentence, it is available at any time rather than only
// in the instant of the call, and it needs no event sink at all.

using System;
using System.Runtime.InteropServices;

namespace TargetCom
{
    /// <summary>One P2P messaging endpoint. Wraps p2pf::IP2PHub.</summary>
    // The IID was REISSUED for ABI 4, when the eight typed Listen*/Connect*
    // methods collapsed onto one endpoint pair. An interface is immutable once
    // anyone holds it, so the shape change had to come with a new identity --
    // and the old value {8E203CC9-9100-4812-936C-0AD3263C30AD} is now the
    // strongest safety net this file has. Leave a stale copy of this interop
    // against a current server and the cast fails at QueryInterface, loudly,
    // instead of calling Listen with an int where a BSTR belongs.
    [ComImport]
    [Guid("12D65CF2-3F8C-4856-A52F-1B0B35F3B4FE")]
    [InterfaceType(ComInterfaceType.InterfaceIsDual)]
    public interface IP2PHubCom
    {
        // --- connections ------------------------------------------------------
        // One pair for every transport. `endpoint` is "tcp://:7788",
        // "tcp://127.0.0.1:7788", "pipe://name", "dmx://service" or
        // "serial://COM5" -- see Com.Endpoint in ComHarness.cs.
        //
        // These are the only two members that can return a SUCCESS code other
        // than S_OK, and `void` throws it away. See the header for why they are
        // still declared this way.
        [DispId(1)] void Listen  ([MarshalAs(UnmanagedType.BStr)] string toPeer,
                                  [MarshalAs(UnmanagedType.BStr)] string endpoint);

        [DispId(2)] void Connect ([MarshalAs(UnmanagedType.BStr)] string toPeer,
                                  [MarshalAs(UnmanagedType.BStr)] string endpoint);

        // --- sending -----------------------------------------------------------
        // payload is a VARIANT on purpose: a byte[] from here, a string from
        // VBScript, and nothing else -- see PipeMsgFactoryTest.
        [DispId(3)]  void Send          ([MarshalAs(UnmanagedType.BStr)]   string dest,
                                         [MarshalAs(UnmanagedType.BStr)]   string topic,
                                         [MarshalAs(UnmanagedType.Struct)] object payload);
        [DispId(4)]  void SendText      ([MarshalAs(UnmanagedType.BStr)] string dest,
                                         [MarshalAs(UnmanagedType.BStr)] string topic,
                                         [MarshalAs(UnmanagedType.BStr)] string text);

        // Delivered comes back as a VALUE, because the facade signals "nobody was
        // up" with S_FALSE and an automation client never sees a success HRESULT.
        // Which is the same problem P2PF_S_UNRELATED_LINK has, solved the other
        // way round: the IDL reshaped this one, and PreserveSig rescues that one.
        [DispId(5)]
        [return: MarshalAs(UnmanagedType.VariantBool)]
        bool Broadcast ([MarshalAs(UnmanagedType.BStr)]   string topic,
                        [MarshalAs(UnmanagedType.Struct)] object payload);

        // --- properties / lifecycle ---------------------------------------------
        string Address { [DispId(6)] [return: MarshalAs(UnmanagedType.BStr)] get; }

        [DispId(7)]
        [return: MarshalAs(UnmanagedType.VariantBool)]
        bool IsPeerUp ([MarshalAs(UnmanagedType.BStr)] string peer);

        [DispId(8)] void Close ();

        // --- the read side ------------------------------------------------------
        //
        // APPENDED to the interface, dispids 9-13, WITHOUT a new IID: an addition
        // after the last member leaves 1-8 at their slots and their names, so this
        // file's existing declarations did not move and no call site changed.
        //
        // This is the answer to the blind spot in the header above. Listen/Connect
        // still cannot tell a .NET caller that an edge is a sibling one -- a `void`
        // signature discards the success HRESULT and nothing recovers it -- but the
        // hub can be ASKED, at any time after the fact:
        //
        //     if ((hub.RelationTo(peer) & (int)P2PConFlag.RelUnrelated) != 0) ...
        //     Log.Debug(hub.Description);
        //
        // ONE HAZARD comes with adding members under an unchanged IID, and it is
        // worth knowing rather than discovering: these five have no protection
        // against a STALE registered server. The IID that guards the rest of this
        // file cannot help, because it did not change -- so a call to dispid 9 on
        // a pre-read-side build is a call past the end of that build's vtable.
        // Unregister an old TargetCom before running against this one.
        [DispId(9)] int RelationTo ([MarshalAs(UnmanagedType.BStr)] string peer);

        int ConCount { [DispId(10)] get; }

        [DispId(11)]
        [return: MarshalAs(UnmanagedType.BStr)]
        string PeerAt (int index);

        [DispId(12)]
        [return: MarshalAs(UnmanagedType.BStr)]
        string EndpointFor ([MarshalAs(UnmanagedType.BStr)] string peer);

        string Description { [DispId(13)] [return: MarshalAs(UnmanagedType.BStr)] get; }
    }

    /// <summary>
    /// What RelationTo returns: two independent halves of one int.
    ///
    /// What THIS HUB DID -- ConListen / ConDial / ConUp. Neither of the first two
    /// is set when a wildcard listener adopted the peer's name at login: the hub
    /// armed nothing for it specifically and has no endpoint of its own to report.
    ///
    /// WHERE THE PEER SITS in the dotted address tree, relative to this hub.
    /// EXACTLY ONE of the Rel* bits is set. RelUnrelated is the one to branch on:
    /// the edge is armed and carries direct traffic, but it can never be a transit
    /// hop, so anything addressed BEYOND that peer is silently undeliverable and a
    /// broadcast stops there instead of relaying onward.
    /// </summary>
    [Flags]
    public enum P2PConFlag
    {
        ConListen     = 0x0001,
        ConDial       = 0x0002,
        ConUp         = 0x0004,
        RelSelf       = 0x0100,
        RelDescendant = 0x0200,
        RelAncestor   = 0x0400,
        RelUnrelated  = 0x0800,
        RelPattern    = 0x1000
    }

    /// <summary>The one init object. Wraps p2pf::IP2PNetwork.</summary>
    [ComImport]
    [Guid("319ECAAE-00D7-4B11-AA4D-94BF2934F5F8")]
    [InterfaceType(ComInterfaceType.InterfaceIsDual)]
    public interface IP2PNetworkCom
    {
        [DispId(1)]
        [return: MarshalAs(UnmanagedType.Interface)]
        IP2PHubCom CreateHub ([MarshalAs(UnmanagedType.BStr)] string address);

        string VersionString { [DispId(2)] [return: MarshalAs(UnmanagedType.BStr)] get; }
        int    MaxPayload    { [DispId(3)] get; }
    }

    /// <summary>
    /// The source dispinterface. Raised on a dispatch thread inside the DLL and
    /// marshalled into the sink's apartment -- which is why every harness pumps.
    /// </summary>
    [ComImport]
    [Guid("5EFF43D2-F741-489E-B090-612904CC3CA8")]
    [InterfaceType(ComInterfaceType.InterfaceIsIDispatch)]
    public interface IP2PHubEvents
    {
        [DispId(1)] void OnMessage  ([MarshalAs(UnmanagedType.BStr)]        string source,
                                     [MarshalAs(UnmanagedType.BStr)]        string topic,
                                     [MarshalAs(UnmanagedType.Struct)]      object payload,
                                     [MarshalAs(UnmanagedType.VariantBool)] bool   broadcast);
        [DispId(2)] void OnPeerUp   ([MarshalAs(UnmanagedType.BStr)] string peer);
        [DispId(3)] void OnPeerDown ([MarshalAs(UnmanagedType.BStr)] string peer);
        [DispId(4)] void OnError    ([MarshalAs(UnmanagedType.BStr)] string what);
    }

    /// <summary>coclass TargetCom.P2PNetwork -- the only creatable one.</summary>
    [ComImport]
    [Guid("AC8A3AD3-F73E-467C-952C-AED2762C00C3")]
    public class P2PNetworkClass { }

    /// <summary>
    /// The HRESULTs the facade defines and the COM layer passes through, plus the
    /// handful of standard ones these harnesses actually branch on.
    /// </summary>
    public static class Hr
    {
        public const int S_OK    = 0;
        public const int S_FALSE = 1;

        public const int E_INVALIDARG          = unchecked((int)0x80070057);
        public const int DISP_E_TYPEMISMATCH   = unchecked((int)0x80020005);
        public const int REGDB_E_CLASSNOTREG   = unchecked((int)0x80040154);

        // FACILITY_ITF 0x0200. Note this is numerically CONNECT_E_NOCONNECTION
        // too -- FACILITY_ITF codes are only meaningful per interface, which is
        // exactly why the facade documents its own range.
        public const int CONNECT_E_NOCONNECTION = unchecked((int)0x80040200);

        public const int P2PF_E_ABI_MISMATCH   = unchecked((int)0x80040200);
        public const int P2PF_E_STARTUP        = unchecked((int)0x80040201);
        public const int P2PF_E_HUB_SPAWN      = unchecked((int)0x80040202);
        public const int P2PF_E_CON_FACTORY    = unchecked((int)0x80040203);
        public const int P2PF_E_CON_DUPLICATE  = unchecked((int)0x80040204);
        public const int P2PF_E_RESERVED_TOPIC = unchecked((int)0x80040205);
        public const int P2PF_E_CLOSED         = unchecked((int)0x80040206);
        public const int P2PF_E_HUB_DUPLICATE  = unchecked((int)0x80040207);
        public const int P2PF_E_ENDPOINT       = unchecked((int)0x80040208);
        public const int P2PF_E_UNRESOLVED     = unchecked((int)0x80040209);
        public const int P2PF_E_NO_HUB         = unchecked((int)0x8004020A);
        public const int P2PF_E_LINK_PARTIAL   = unchecked((int)0x8004020B);

        /// <summary>
        /// NOT an error. Severity 0: the link IS armed, but the two addresses
        /// are neither ancestor nor descendant, so nothing can be routed
        /// through the edge. Every X.Server/X.Client pair in this tree gets it.
        ///
        /// Declared for completeness and for Name(), but **unreachable through
        /// this interop**: the marshaller discards a successful HRESULT from a
        /// `void` signature, so Com.Hub.Listen reports S_OK. The same fact
        /// arrives on OnError instead. See the header of this file.
        /// </summary>
        public const int P2PF_S_UNRELATED_LINK = 0x0004020C;

        /// <summary>The FAILED() macro. COM severity lives in the top bit.</summary>
        public static bool Failed (int hr) { return hr < 0; }

        public static string Name (int hr)
        {
            switch (hr)
            {
                case S_OK:                   return "S_OK";
                case S_FALSE:                return "S_FALSE";
                case P2PF_S_UNRELATED_LINK:  return "P2PF_S_UNRELATED_LINK";
                case E_INVALIDARG:           return "E_INVALIDARG";
                case DISP_E_TYPEMISMATCH:    return "DISP_E_TYPEMISMATCH";
                case REGDB_E_CLASSNOTREG:    return "REGDB_E_CLASSNOTREG";
                case P2PF_E_CON_FACTORY:     return "P2PF_E_CON_FACTORY";
                case P2PF_E_CON_DUPLICATE:   return "P2PF_E_CON_DUPLICATE";
                case P2PF_E_RESERVED_TOPIC:  return "P2PF_E_RESERVED_TOPIC";
                case P2PF_E_CLOSED:          return "P2PF_E_CLOSED";
                case P2PF_E_HUB_DUPLICATE:   return "P2PF_E_HUB_DUPLICATE";
                case P2PF_E_ENDPOINT:        return "P2PF_E_ENDPOINT";
                case P2PF_E_UNRESOLVED:      return "P2PF_E_UNRESOLVED";
                case P2PF_E_NO_HUB:          return "P2PF_E_NO_HUB";
                case P2PF_E_LINK_PARTIAL:    return "P2PF_E_LINK_PARTIAL";
                default:                     return "(other)";
            }
        }
    }
}

' Copyright © 2026 Khrustal & Mann
'              MELBOURNE, VICTORIA, AUSTRALIA, 3000
'
' Licensed under the Apache License, Version 2.0 (the "License");
' you may not use this file except in compliance with the License.
' You may obtain a copy of the License at
'
'     http://www.apache.org/licenses/LICENSE-2.0
'
' Unless required by applicable law or agreed to in writing, software
' distributed under the License is distributed on an "AS IS" BASIS,
' WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
' implied. See the License for the specific language governing
' permissions and limitations under the License.
'
' vbs_client.vbs -- the same thing again from the oldest scripting host there is.
'
' If TargetCom works from VBScript under cscript, it works from VBA, from an
' Excel macro, from a WSH logon script and from classic ASP. That is a claim
' worth testing rather than assuming, so this file tests it.
'
' VBScript has no byte arrays worth the name, no structures and no HRESULT
' inspection beyond Err.Number -- so this is the LEAST capable client the layer
' will ever have, which makes it the most useful one to prove.
'
' Events: not sinked here. WSH can only sink events for an object it created
' itself via WScript.CreateObject(progid, prefix), and hubs come from
' Network.CreateHub. Delivery is therefore observed the same way ps_client.ps1
' observes it -- through IsPeerUp and Broadcast's return value, both of which
' only become true after real traffic moved.
'
'   cscript //nologo vbs_client.vbs [binDir]
'
' Exit code = number of failed checks.

Option Explicit

Dim fails, net, server, client, deadline, ok, endpoint
fails = 0

Sub Check(cond, what)
    If cond Then
        WScript.Echo "  ok    " & what
    Else
        WScript.Echo "  FAIL  " & what
        fails = fails + 1
    End If
End Sub

' The kernel DLLs resolve against the process directory. cscript's is
' system32, so the caller passes the staging directory and we chdir into it.
If WScript.Arguments.Count >= 1 Then
    Dim shell
    Set shell = CreateObject("WScript.Shell")
    shell.CurrentDirectory = WScript.Arguments(0)
End If

WScript.Echo "=== vbs_client.vbs - TargetCom from VBScript ==="

On Error Resume Next
Set net = CreateObject("TargetCom.P2PNetwork")
If Err.Number <> 0 Then
    WScript.Echo "  FAIL  CreateObject(TargetCom.P2PNetwork): " & Err.Description
    WScript.Quit 1
End If
On Error Goto 0

Check True, "CreateObject(""TargetCom.P2PNetwork"")"
WScript.Echo "        " & net.VersionString
Check (net.MaxPayload = 24576), "MaxPayload reads as a property"

Set server = net.CreateHub("Vbs.Server")
Set client = net.CreateHub("Vbs.Client")
Check (server.Address = "Vbs.Server"), "hub.Address reads as a property"

' Dmx never retries: arm the listener first.
'
' There is ONE Listen/Connect pair for all four transports now, and the
' transport is the scheme inside the endpoint string. This is the floor of the
' whole client list, and it loses nothing: a string is the one type VBScript
' has always had, and string concatenation is the one thing it has always been
' able to do. Composing "dmx://" & name is strictly easier here than choosing
' between four differently-named methods.
endpoint = "dmx://" & "VbsDemoCom"
server.Listen "Vbs.Client", endpoint
client.Connect "Vbs.Server", endpoint

deadline = Timer + 15
Do While (Not client.IsPeerUp("Vbs.Server")) And Timer < deadline
    WScript.Sleep 50
Loop
Check client.IsPeerUp("Vbs.Server"), "Dmx link completed the login handshake"
Check server.IsPeerUp("Vbs.Client"), "...and the listening side agrees"

' The side that dialled speaks first.
client.SendText "Vbs.Server", "greeting", "hello from VBScript"
Check True, "SendText took a plain string"

' Send accepts a string in the payload VARIANT too, which is the only thing a
' VBScript caller can build -- that is exactly why the IDL takes a VARIANT
' there rather than a SAFEARRAY(VT_UI1) parameter.
client.Send "Vbs.Server", "bin", "payload as a string"
Check True, "Send accepted a string in the payload VARIANT"

' True only if a peer was up to take a copy.
Check (server.Broadcast("news", "to everyone") = True), _
      "Broadcast returned True, so a peer took a copy"

' The facade's HRESULTs arrive as trappable script errors.
On Error Resume Next
Err.Clear
client.SendText "Vbs.Server", "P2Pmsg_Nope", "x"
Check (Err.Number <> 0), "reserved topic raised a script error (P2PF_E_RESERVED_TOPIC)"
Err.Clear

' An unparseable endpoint is one of those too. VBScript cannot read the HRESULT
' value, only that something was raised -- which is precisely why the grammar
' has to reject a bad endpoint rather than quietly do something with it.
client.Listen "Vbs.Server", "not-an-endpoint"
Check (Err.Number <> 0), "a bad endpoint raised a script error (P2PF_E_ENDPOINT)"
Err.Clear
On Error Goto 0

client.Close
server.Close

On Error Resume Next
Err.Clear
server.Listen "Vbs.Client", endpoint
Check (Err.Number <> 0), "a closed hub raised a script error (P2PF_E_CLOSED)"
Err.Clear
On Error Goto 0

WScript.Echo ""
WScript.Echo fails & " failure(s)"
WScript.Quit fails

# Copyright © 2026 Khrustal & Mann
#              MELBOURNE, VICTORIA, AUSTRALIA, 3000
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
# implied. See the License for the specific language governing
# permissions and limitations under the License.
#
# ps_client.ps1 -- the reason the COM layer exists, in one page.
#
# No compiler, no header, no import lib, no type library reference: a late-bound
# PowerShell client driving the same hubs the C++ harnesses do, through
# IDispatch and the registered coclass. Two hubs in one process over Dmx, so
# nothing needs a TCP port.
#
# ON EVENTS -- the honest limitation, stated once. This script does NOT sink
# _IP2PHubEvents. That is a HOST limitation, not a gap in the layer: .NET (so
# PowerShell) can only bind COM events through an interop assembly for the
# coclass, which needs TlbImp or an early-bound reference. The C++ harnesses in
# this tree attach to the same connection point and get
# OnMessage/OnPeerUp/OnPeerDown/OnError; so would VB6, or C# with an interop.
#
# What a script CAN observe end to end is delivery itself:
#   * IsPeerUp only goes True after the kernel's login handshake completes;
#   * Broadcast returns True only if at least one peer was up to take a copy.
# Both are real proof that traffic moved, and both are used below.
#
#   ..\run_all.ps1 -IncludeScripts      # registers, runs this, unregisters
#
# Exit code = number of failed checks.

[CmdletBinding()]
param([ValidateSet('Debug','Release')] [string] $Config = 'Debug')

$ErrorActionPreference = 'Stop'
$here    = Split-Path -Parent $MyInvocation.MyCommand.Path
$binDir  = Join-Path $here "..\out\x64\$Config"

# The kernel DLLs resolve against the PROCESS directory, so run from where they
# were staged.
Push-Location $binDir

$script:fails = 0
function Check($ok, $what) {
    if ($ok) { Write-Host "  ok    $what" }
    else     { Write-Host "  FAIL  $what"; $script:fails++ }
}
function HResultOf([scriptblock] $sb) {
    try { & $sb; return '(no error)' }
    catch { return '0x{0:X8}' -f $_.Exception.HResult }   # HResult is a signed Int32
}

try {
    $net = New-Object -ComObject TargetCom.P2PNetwork
    Check ($null -ne $net) "New-Object -ComObject TargetCom.P2PNetwork"
    Write-Host "        $($net.VersionString)"
    Check ($net.MaxPayload -eq 24576) "MaxPayload reads as a property"

    $server = $net.CreateHub("Script.Server")
    $client = $net.CreateHub("Script.Client")
    Check ($server.Address -eq "Script.Server") "hub.Address reads as a property"

    # Dmx never retries, so arm the listener first.
    #
    # ONE Listen/Connect pair now covers all four transports; the transport is
    # the scheme in the endpoint string. For a late-bound caller that is a
    # strict gain -- an endpoint is a *value*, so it can come from a parameter,
    # a config file or a registry key, whereas picking between ListenDmx and
    # ListenPipe meant branching on the method name at authoring time.
    $endpoint = "dmx://ScriptDemoCom"
    $server.Listen("Script.Client", $endpoint)
    $client.Connect("Script.Server", $endpoint)

    $deadline = (Get-Date).AddSeconds(15)
    while (-not $client.IsPeerUp("Script.Server") -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 50
    }
    Check ($client.IsPeerUp("Script.Server")) "Dmx link completed the login handshake"
    Check ($server.IsPeerUp("Script.Client")) "...and the listening side agrees"

    # The dialling side speaks first -- the ordering rule the C++ harnesses
    # document. A message from the listener here could arrive before the far end
    # has finished logging in, and the kernel would drop the connection.
    $client.SendText("Script.Server", "greeting", "hello from PowerShell")
    Check $true "SendText took a plain string, no marshalling ceremony"

    # A byte payload from script: PowerShell hands a byte[] over as
    # SAFEARRAY(VT_UI1), which is exactly what the VARIANT parameter accepts.
    $client.Send("Script.Server", "bin", [byte[]](1..64))
    Check $true "Send took a byte[] as SAFEARRAY(VT_UI1)"

    # True only if a peer was actually up to receive a copy -- real delivery.
    Check ($server.Broadcast("news", "to everyone") -eq $true) `
          "Broadcast returned True, so a peer took a copy"

    # The facade's own HRESULTs arrive intact as COMExceptions.
    Check ((HResultOf { $client.SendText("Script.Server", "P2Pmsg_Nope", "x") }) -eq '0x80040205') `
          "reserved topic surfaces as P2PF_E_RESERVED_TOPIC (0x80040205)"
    # The bad-port check used to be E_INVALIDARG out of the COM layer's own
    # range check on a LONG parameter. The port is text now, so the endpoint
    # parser rejects it -- same mistake, same call, one rule instead of two.
    Check ((HResultOf { $client.Listen("Script.Server", "tcp://:70000") }) -eq '0x80040208') `
          "out-of-range port surfaces as P2PF_E_ENDPOINT (0x80040208)"
    Check ((HResultOf { $client.Listen("Script.Server", "carrier-pigeon://nope") }) -eq '0x80040208') `
          "an unknown scheme surfaces as P2PF_E_ENDPOINT too"

    $client.Close()
    $server.Close()
    Check ((HResultOf { $server.Listen("Script.Client", $endpoint) }) -eq '0x80040206') `
          "a closed hub surfaces P2PF_E_CLOSED (0x80040206)"

    [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($net)
}
finally {
    Pop-Location
}

Write-Host "`n$script:fails failure(s)"
exit $script:fails

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
# run_all.ps1 -- build and run every Java harness, and report the exit codes.
#
# Every harness reports its verdict the same way the originals did:
#   0 = SUCCESS   1 = SETUP failure   3 = TIMEOUT / expectation not met
#
# Two of them are two-PROCESS tests (AlexTest, AlexInterop): this script starts
# the server, then the client, and requires BOTH to exit 0.
#
#   .\run_all.ps1                    # build + run Debug
#   .\run_all.ps1 -Config Release
#   .\run_all.ps1 -NoBuild
#
# Com232MeshTest needs a com0com null-modem pair on COM5<->COM6. Without one it
# reports SETUP (exit 1); that is a missing prerequisite, not a failure, and the
# summary says so.

[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')] [string] $Config = 'Debug',
    [switch] $NoBuild
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$bin  = Join-Path $here "bin\$Config"
$out  = Join-Path $here "out\$Config"
$logs = Join-Path $here "logs\$Config"

if (-not $NoBuild) {
    & (Join-Path $here 'build.ps1') -Config $Config | Out-Null
}

$java = if ($env:JAVA_HOME -and (Test-Path "$env:JAVA_HOME\bin\java.exe")) {
    "$env:JAVA_HOME\bin\java.exe"
} else {
    (Get-Command java -ErrorAction Stop).Source
}

New-Item -ItemType Directory -Force -Path $logs | Out-Null

# --enable-native-access=ALL-UNNAMED silences the JDK 24+ restricted-method
# warning at run time. It is required from JDK 25 on, where an unnamed module
# calling a restricted method is an error rather than a warning -- so passing it
# now is what keeps this tree running on the next LTS without an edit.
#
# The working directory is bin\<Config>: mscs.p2pf.Network looks for
# TargetFacade.dll relative to it, and Windows resolves that DLL's own
# dependencies out of the same folder.
$jvmArgs = @('--enable-native-access=ALL-UNNAMED', '-cp', $out)

$single = @(
    'WsaMeshTest', 'PipeMeshTest', 'DmxMeshTest', 'Com232MeshTest',
    'LocalInMemoryTest', 'PipeMsgMapTest', 'PipeMsgFactoryTest',
    'TwoConTest', 'RouteLoopbackTest', 'ExplorerTest'
)

$results = [System.Collections.ArrayList]::new()

foreach ($name in $single) {
    $log = Join-Path $logs "$name.txt"
    $p = Start-Process -FilePath $java -WorkingDirectory $bin -PassThru -NoNewWindow `
                       -ArgumentList ($jvmArgs + "mscs.examples.$name") `
                       -RedirectStandardOutput $log -RedirectStandardError (Join-Path $logs "$name.err.txt")
    $p.WaitForExit(90000) | Out-Null
    [void]$results.Add([pscustomobject]@{ Harness = $name; Exit = $p.ExitCode })
}

# Two-process harnesses: server first, then client; both must exit 0.
function Run-Pair([string] $name, [string[]] $serverArgs, [string[]] $clientArgs) {
    $srv = Start-Process -FilePath $java -WorkingDirectory $bin -PassThru -NoNewWindow `
                         -ArgumentList ($jvmArgs + "mscs.examples.$name" + $serverArgs) `
                         -RedirectStandardOutput (Join-Path $logs "$name.server.txt") `
                         -RedirectStandardError  (Join-Path $logs "$name.server.err.txt")
    # A JVM takes appreciably longer to reach its first line than a native exe
    # does, so the head start here is bigger than the C++ trees need. It is not
    # load-bearing -- a facade tcp:// dial retries until the far side answers --
    # but it keeps the logs readable in the order the events happened.
    Start-Sleep -Milliseconds 1500
    $cli = Start-Process -FilePath $java -WorkingDirectory $bin -PassThru -NoNewWindow `
                         -ArgumentList ($jvmArgs + "mscs.examples.$name" + $clientArgs) `
                         -RedirectStandardOutput (Join-Path $logs "$name.client.txt") `
                         -RedirectStandardError  (Join-Path $logs "$name.client.err.txt")
    $cli.WaitForExit(90000) | Out-Null
    $srv.WaitForExit(90000) | Out-Null

    [void]$results.Add([pscustomobject]@{ Harness = "$name (server)"; Exit = $srv.ExitCode })
    [void]$results.Add([pscustomobject]@{ Harness = "$name (client)"; Exit = $cli.ExitCode })
}

Run-Pair 'AlexTest'     @()                 @('send','127.0.0.1','hello-from-run_all')
Run-Pair 'AlexInterop'  @('server','7811')  @('send','127.0.0.1','7811','hello-two-process')

# ---- summary -------------------------------------------------------------
Write-Host ""
$pass = 0; $fail = 0; $setup = 0
foreach ($r in $results) {
    switch ($r.Exit) {
        0       { $verdict = 'PASS';  $pass++ }
        1       { $verdict = 'SETUP'; $setup++ }
        default { $verdict = 'FAIL';  $fail++ }
    }
    '{0,-32} exit={1}  {2}' -f $r.Harness, $r.Exit, $verdict | Write-Host
}

Write-Host ""
Write-Host "$Config : $pass passed, $fail failed, $setup missing-prerequisite"
Write-Host "logs in $logs"
exit $fail

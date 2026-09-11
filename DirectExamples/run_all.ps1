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
# run_all.ps1 -- build the tree and run every harness that CAN be run unattended,
# then report the exit codes.
#
# Every harness reports its verdict the same way:
#   0 = SUCCESS   1 = SETUP failure   2 = MFC/CRT assertion   3 = TIMEOUT
#
#   .\run_all.ps1                    # build + run Debug
#   .\run_all.ps1 -Config Release
#   .\run_all.ps1 -NoBuild
#
# THREE HARNESSES ARE NOT RUN THE ORDINARY WAY, and the reasons are not
# interchangeable -- do not "fix" one by copying what was done for another.
#
#   AlexTest           EXCLUDED ENTIRELY. Its server blocks on getchar() waiting
#                      for a human, and main() returns 0 unconditionally, so its
#                      exit code carries no verdict even if it did finish. It is
#                      interactive by design; AlexInterop is the automatable
#                      rewrite and is what this script runs instead. See About.md.
#
#   AlexInterop        Run as a PAIR: server first, client second, both must exit
#                      0. It is the only harness here that crosses a real process
#                      boundary, and the server's 0 is the authoritative signal.
#
#   RouteLoopbackTest  Run if it was built. It links treehub_runtime.lib from
#                      $(KgnRoot), a separate project that is not part of the MSCS
#                      tree, so on most machines it does not build at all. A
#                      missing exe is reported as SKIP, not as a failure -- but
#                      ONLY for this one project. Any other missing exe is a real
#                      build failure and is reported as such.
#
# Com232MeshTest needs a com0com null-modem pair on COM5<->COM6 (setupc install
# PortName=COM5 PortName=COM6). Without one it reports SETUP (exit 1); that is a
# missing prerequisite, not a failure, and the summary counts it separately.

[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')] [string] $Config = 'Debug',
    [switch] $NoBuild
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$bin  = Join-Path $here "out\x64\$Config"
$logs = Join-Path $here "logs\$Config"

if (-not $NoBuild) {
    $msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
                 -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe |
               Select-Object -First 1
    if (-not $msbuild) { throw "MSBuild not found" }

    Write-Host "building $Config..."
    & $msbuild (Join-Path $here 'DirectExamples(2026).sln') `
        -p:Configuration=$Config -p:Platform=x64 -v:minimal -nologo -m |
        Where-Object { $_ -match 'error|warning C' }
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
}

New-Item -ItemType Directory -Force -Path $logs | Out-Null

# Single-process harnesses: run, collect the exit code.
$single = @(
    'WsaMeshTest', 'PipeMeshTest', 'DmxMeshTest', 'Com232MeshTest',
    'LocalInMemoryTest', 'PipeMsgMapTest', 'PipeMsgFactoryTest',
    'TwoConTest', 'ExplorerTest', 'LoginAckDomainTest', 'RouteLoopbackTest'
)

# The only project allowed to be absent without that being a build failure.
$optional = @('RouteLoopbackTest')

$results = [System.Collections.ArrayList]::new()

# Wait for a process, and KILL it if it overruns. Without the kill, a harness
# that hangs leaves $p.ExitCode unreadable (it throws on a live process) and the
# script dies with an exception instead of reporting a timeout -- which is the
# one verdict a harness runner most needs to be able to report. 3 is the tree's
# own TIMEOUT code, so a killed run reads the same as a self-detected one.
function Wait-Harness([System.Diagnostics.Process] $p, [int] $ms = 60000) {
    if ($p.WaitForExit($ms)) { return $p.ExitCode }
    Write-Warning ("{0} did not exit within {1}s -- killing it." -f $p.ProcessName, ($ms / 1000))
    try { $p.Kill($true) } catch { }
    $p.WaitForExit(5000) | Out-Null
    return 3
}

foreach ($name in $single) {
    $exe = Join-Path $bin "$name.exe"
    if (-not (Test-Path $exe)) {
        if ($optional -contains $name) {
            [void]$results.Add([pscustomobject]@{ Harness = $name; Exit = $null })
            continue
        }
        throw "missing $exe"
    }
    $log = Join-Path $logs "$name.txt"
    $p = Start-Process -FilePath $exe -WorkingDirectory $bin -PassThru -NoNewWindow `
                       -RedirectStandardOutput $log
    [void]$results.Add([pscustomobject]@{ Harness = $name; Exit = (Wait-Harness $p) })
}

# Two-process harness: server first, then client; both must exit 0.
function Invoke-Pair([string] $name, [string[]] $serverArgs, [string[]] $clientArgs) {
    $exe = Join-Path $bin "$name.exe"
    if (-not (Test-Path $exe)) { throw "missing $exe" }

    $srv = Start-Process -FilePath $exe -ArgumentList $serverArgs -WorkingDirectory $bin `
                         -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $logs "$name.server.txt")
    Start-Sleep -Milliseconds 500
    $cli = Start-Process -FilePath $exe -ArgumentList $clientArgs -WorkingDirectory $bin `
                         -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $logs "$name.client.txt")
    $cliExit = Wait-Harness $cli
    $srvExit = Wait-Harness $srv

    [void]$results.Add([pscustomobject]@{ Harness = "$name (server)"; Exit = $srvExit })
    [void]$results.Add([pscustomobject]@{ Harness = "$name (client)"; Exit = $cliExit })
}

Invoke-Pair 'AlexInterop' @('server','7811') @('send','127.0.0.1','7811','hello-two-process')

# ---- summary -------------------------------------------------------------
Write-Host ""
$pass = 0; $fail = 0; $setup = 0; $skip = 0
foreach ($r in $results) {
    if ($null -eq $r.Exit) {
        $verdict = 'SKIP (not built)'; $skip++
        '{0,-24} {1}' -f $r.Harness, $verdict | Write-Host
        continue
    }
    switch ($r.Exit) {
        0       { $verdict = 'PASS';    $pass++ }
        1       { $verdict = 'SETUP';   $setup++ }
        default { $verdict = 'FAIL';    $fail++ }
    }
    '{0,-24} exit={1}  {2}' -f $r.Harness, $r.Exit, $verdict | Write-Host
}

Write-Host ""
Write-Host "$Config : $pass passed, $fail failed, $setup missing-prerequisite, $skip skipped"
Write-Host "AlexTest is not run: it is interactive by design (see About.md)."
Write-Host "logs in $logs"
exit $fail

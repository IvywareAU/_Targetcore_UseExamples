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
# run_all.ps1 -- build and run every Light harness, and report the exit codes.
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
$bin  = Join-Path $here "out\x64\$Config"
$logs = Join-Path $here "logs\$Config"

if (-not $NoBuild) {
    $msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
                 -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe |
               Select-Object -First 1
    if (-not $msbuild) { throw "MSBuild not found" }

    Write-Host "building $Config..."
    & $msbuild (Join-Path $here 'FacadeExamples(2026).sln') `
        -p:Configuration=$Config -p:Platform=x64 -v:minimal -nologo -m |
        Where-Object { $_ -match 'error|warning C' }
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
}

New-Item -ItemType Directory -Force -Path $logs | Out-Null

# Single-process harnesses: run, collect the exit code.
$single = @(
    'WsaMeshTestLight', 'PipeMeshTestLight', 'DmxMeshTestLight', 'Com232MeshTestLight',
    'LocalInMemoryTestLight', 'PipeMsgMapTestLight', 'PipeMsgFactoryTestLight',
    'TwoConTestLight', 'RouteLoopbackTestLight'
)

$results = [System.Collections.ArrayList]::new()

foreach ($name in $single) {
    $exe = Join-Path $bin "$name.exe"
    if (-not (Test-Path $exe)) { throw "missing $exe" }
    $log = Join-Path $logs "$name.txt"
    $p = Start-Process -FilePath $exe -WorkingDirectory $bin -PassThru -NoNewWindow `
                       -RedirectStandardOutput $log
    $p.WaitForExit(60000) | Out-Null
    [void]$results.Add([pscustomobject]@{ Harness = $name; Exit = $p.ExitCode })
}

# Two-process harnesses: server first, then client; both must exit 0.
function Run-Pair([string] $name, [string[]] $serverArgs, [string[]] $clientArgs) {
    $exe = Join-Path $bin "$name.exe"
    if (-not (Test-Path $exe)) { throw "missing $exe" }

    $srv = Start-Process -FilePath $exe -ArgumentList $serverArgs -WorkingDirectory $bin `
                         -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $logs "$name.server.txt")
    Start-Sleep -Milliseconds 500
    $cli = Start-Process -FilePath $exe -ArgumentList $clientArgs -WorkingDirectory $bin `
                         -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $logs "$name.client.txt")
    $cli.WaitForExit(60000) | Out-Null
    $srv.WaitForExit(60000) | Out-Null

    [void]$results.Add([pscustomobject]@{ Harness = "$name (server)"; Exit = $srv.ExitCode })
    [void]$results.Add([pscustomobject]@{ Harness = "$name (client)"; Exit = $cli.ExitCode })
}

Run-Pair 'AlexTestLight'     @()                  @('send','127.0.0.1','hello-from-run_all')
Run-Pair 'AlexInteropLight'  @('server','7811')   @('send','127.0.0.1','7811','hello-two-process')

# ---- summary -------------------------------------------------------------
Write-Host ""
$pass = 0; $fail = 0; $setup = 0
foreach ($r in $results) {
    switch ($r.Exit) {
        0       { $verdict = 'PASS';    $pass++ }
        1       { $verdict = 'SETUP';   $setup++ }
        default { $verdict = 'FAIL';    $fail++ }
    }
    '{0,-32} exit={1}  {2}' -f $r.Harness, $r.Exit, $verdict | Write-Host
}

Write-Host ""
Write-Host "$Config : $pass passed, $fail failed, $setup missing-prerequisite"
Write-Host "logs in $logs"
exit $fail

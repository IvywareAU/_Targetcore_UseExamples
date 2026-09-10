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
# run_all.ps1 -- register TargetCom per-user, run every COM harness, unregister.
#
# Registration is `regsvr32 /n /i:user`, i.e. our DllInstall with
# AtlSetPerUserRegistration -- it writes HKCU\Software\Classes only, needs no
# elevation, touches nothing machine-wide, and is always removed again.
#
# The STAGED copy in bin\<Config> is what gets registered, so this directory is
# self-contained and nothing here depends on the TargetFacade tree's own build
# output being registered.
#
# Every harness reports its verdict the same way the other two trees do:
#   0 = SUCCESS   1 = SETUP failure   3 = TIMEOUT / expectation not met
#
#   .\run_all.ps1                    # build + run Debug
#   .\run_all.ps1 -Config Release
#   .\run_all.ps1 -NoBuild
#   .\run_all.ps1 -IncludeScripts    # also run the late-bound PowerShell/VBScript clients

[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')] [string] $Config = 'Debug',
    [switch] $NoBuild,
    [switch] $IncludeScripts
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
    & $msbuild (Join-Path $here 'ComExamples(2026).sln') `
        -p:Configuration=$Config -p:Platform=x64 -v:minimal -nologo -m |
        Where-Object { $_ -match 'error|warning C' }
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
}

New-Item -ItemType Directory -Force -Path $logs | Out-Null

$suffix = if ($Config -eq 'Debug') { 'd' } else { '' }
$comDll = Join-Path $bin "TargetCom.dll"
if (-not (Test-Path $comDll)) { throw "missing $comDll -- build first" }

Write-Host "registering (per-user) $comDll"
$reg = Start-Process regsvr32.exe -ArgumentList '/s','/n','/i:user',"`"$comDll`"" -Wait -PassThru
if ($reg.ExitCode -ne 0) { throw "regsvr32 failed with $($reg.ExitCode)" }

$results = [System.Collections.ArrayList]::new()

try {
    $single = @(
        'WsaMeshTestCom', 'PipeMeshTestCom', 'DmxMeshTestCom', 'Com232MeshTestCom',
        'LocalInMemoryTestCom', 'PipeMsgMapTestCom', 'PipeMsgFactoryTestCom',
        'TwoConTestCom', 'RouteLoopbackTestCom'
    )

    foreach ($name in $single) {
        $exe = Join-Path $bin "$name.exe"
        if (-not (Test-Path $exe)) { throw "missing $exe" }
        $p = Start-Process -FilePath $exe -WorkingDirectory $bin -PassThru -NoNewWindow `
                           -RedirectStandardOutput (Join-Path $logs "$name.txt")
        $p.WaitForExit(60000) | Out-Null
        [void]$results.Add([pscustomobject]@{ Harness = $name; Exit = $p.ExitCode })
    }

    function Run-Pair([string] $name, [string[]] $serverArgs, [string[]] $clientArgs) {
        $exe = Join-Path $bin "$name.exe"
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

    Run-Pair 'AlexTestCom'     @()                @('send','127.0.0.1','hello-from-run_all')
    Run-Pair 'AlexInteropCom'  @('server','7811') @('send','127.0.0.1','7811','hello-two-process')

    if ($IncludeScripts) {
        # The late-bound clients: no compiler, no header, no import lib.
        $ps1    = Join-Path $here 'script\ps_client.ps1'
        $psLog  = Join-Path $logs 'ps_client.txt'
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $ps1 -Config $Config 2>&1 |
            Out-File -FilePath $psLog -Encoding utf8
        [void]$results.Add([pscustomobject]@{ Harness = 'script\ps_client.ps1'; Exit = $LASTEXITCODE })

        $vbs    = Join-Path $here 'script\vbs_client.vbs'
        $vbsLog = Join-Path $logs 'vbs_client.txt'
        & cscript.exe //nologo $vbs $bin 2>&1 |
            Out-File -FilePath $vbsLog -Encoding utf8
        [void]$results.Add([pscustomobject]@{ Harness = 'script\vbs_client.vbs'; Exit = $LASTEXITCODE })
    }
}
finally {
    Start-Process regsvr32.exe -ArgumentList '/s','/u','/n','/i:user',"`"$comDll`"" -Wait | Out-Null
    Write-Host "unregistered"
}

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

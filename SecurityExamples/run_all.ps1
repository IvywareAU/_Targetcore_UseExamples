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
# run_all.ps1 -- build this tree and run both harnesses, then report the exit
# codes. Neither is interactive and neither needs elevation, so unlike the other
# trees in this repository there is nothing here that has to be skipped.
#
#   .\run_all.ps1                    # build + run Debug
#   .\run_all.ps1 -Config Release
#   .\run_all.ps1 -NoBuild
#   .\run_all.ps1 -Fresh             # delete out\...\p2p\ first (see below)
#
# THE VERDICTS ARE THE EXIT CODES, and the two harnesses do not use quite the
# same set. MixConTest uses the repository-wide four:
#
#   0 = PASS   1 = SETUP failure   2 = MFC/CRT assertion   3 = check failed
#
# MixConTestAuth adds a fifth, and the addition is the point of this tree:
#
#   4 = A HUB REFUSED TO ARM
#
# It is separated from 1 deliberately. Every other setup failure is a bug in the
# harness; this one is a FILE -- a missing identity key, an allow-list that names
# nobody, a revocation list that will not load -- and the log names both the
# reason and the path. Reading it as an ordinary setup failure sends you looking
# in the wrong place. This script prints it as its own verdict for that reason.
#
# THE TWO HARNESSES DO NOT COLLIDE. MixConTest binds 127.0.0.1:7799 and the pipe
# \\.\pipe\MixConProbe; MixConTestAuth binds 7801 and \\.\pipe\MixConAuthProbe.
# They are run one after another here anyway, but the endpoints were chosen so
# that a stray instance of one cannot fail the other -- which is what makes a
# leftover process from an earlier debugging session harmless rather than the
# cause of a mystifying SETUP.
#
# -Fresh AND WHAT IT COSTS. MixConTestAuth provisions three identity keys and
# three agreement keys into p2p\ beside its executable on first run, and FINDS
# them on every run after that -- ProvisionAuth is idempotent by design, because
# a first-run helper that regenerated on restart would rotate every hub's
# identity behind the operator's back. So the second run of this script exercises
# the FIND path and not the CREATE path. -Fresh deletes p2p\ so the create path
# is covered too. Run it both ways at least once; a tree that has only ever been
# run -Fresh has never proven the keys persist, and one that never has proves
# nothing about a first run on a clean machine.

[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')] [string] $Config = 'Debug',
    [switch] $NoBuild,
    [switch] $Fresh,
    [int]    $TimeoutSeconds = 90
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$bin  = Join-Path $here "out\x64\$Config"
$logs = Join-Path $here "logs\$Config"

# ---------------------------------------------------------------- build
if (-not $NoBuild) {
    # No null-conditional and no ternary anywhere in this file: it has to run
    # under Windows PowerShell 5.1, which is what powershell.exe still is, and
    # both of those are 7+.
    $msbuild = $null
    $found   = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($found) { $msbuild = $found.Source }
    if (-not $msbuild) {
        $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path $vswhere) {
            $msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild `
                                  -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
        }
    }
    if (-not $msbuild -or -not (Test-Path $msbuild)) {
        Write-Host 'FAIL: msbuild.exe not found. Open a Developer Prompt, or pass -NoBuild.'
        exit 1
    }

    Write-Host "building $Config|x64 ..."
    & $msbuild (Join-Path $here 'SecurityExamples(2026).sln') `
        -p:Configuration=$Config -p:Platform=x64 -v:minimal -nologo -m |
        Where-Object { $_ -match 'error|warning C' }
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAIL: build returned $LASTEXITCODE"
        # The post-build step fails deliberately when the kernel is missing,
        # because TargetCore is DELAY-LOADED here and the alternative is
        # 0xC06D007E at startup with nothing to read. See vsutils\DLL-LOADING.md.
        Write-Host "      If it complained about TargetCore.dll, build TargetCore and"
        Write-Host "      Msgcore first; this tree stages both from ..\..\..\bin\${Config}64."
        exit 1
    }
}

New-Item -ItemType Directory -Force -Path $logs | Out-Null

if ($Fresh) {
    $keys = Join-Path $bin 'p2p'
    if (Test-Path $keys) {
        Write-Host "-Fresh: removing $keys, so MixConTestAuth provisions from scratch."
        Remove-Item -Recurse -Force $keys
    }
}

# Wait for a process and KILL it if it overruns. Without the kill, a harness
# that hangs leaves $p.ExitCode unreadable -- it throws on a live process -- and
# this script would die with an exception instead of reporting a timeout, which
# is the one verdict a harness runner most needs to be able to report. 3 is both
# harnesses' own "check failed / nothing arrived in time" code, so a killed run
# reads the same as a self-detected one.
function Wait-Harness([System.Diagnostics.Process] $p, [int] $ms) {
    if ($p.WaitForExit($ms)) { return $p.ExitCode }
    Write-Warning ("{0} did not exit within {1}s -- killing it." -f $p.ProcessName, ($ms / 1000))
    try { $p.Kill($true) } catch { }
    $p.WaitForExit(5000) | Out-Null
    return 3
}

$results = [System.Collections.ArrayList]::new()

foreach ($name in @('MixConTest', 'MixConTestAuth')) {
    $exe = Join-Path $bin "$name.exe"
    if (-not (Test-Path $exe)) { throw "missing $exe" }
    $log = Join-Path $logs "$name.txt"

    # WorkingDirectory is $bin on purpose. MixConTestAuth resolves p2p\ from
    # GetModuleFileName rather than from the working directory -- so that a
    # clone of the tree elsewhere just works and a stale key from another build
    # tree is never picked up silently -- but its DIAGNOSTICS name paths, and
    # they read as intended only when the two agree.
    $p = Start-Process -FilePath $exe -WorkingDirectory $bin -PassThru -NoNewWindow `
                       -RedirectStandardOutput $log -RedirectStandardError "$log.err"
    [void]$results.Add([pscustomobject]@{
        Harness = $name
        Exit    = (Wait-Harness $p ($TimeoutSeconds * 1000))
    })
}

# ---- summary -------------------------------------------------------------
Write-Host ""
$pass = 0; $fail = 0
foreach ($r in $results) {
    switch ($r.Exit) {
        0       { $verdict = 'PASS';                    $pass++ }
        1       { $verdict = 'SETUP failure';           $fail++ }
        2       { $verdict = 'ASSERTION';               $fail++ }
        3       { $verdict = 'CHECK FAILED / TIMEOUT';  $fail++ }
        4       { $verdict = 'HUB REFUSED TO ARM';      $fail++ }
        default { $verdict = 'FAIL';                    $fail++ }
    }
    '{0,-16} exit={1}  {2}' -f $r.Harness, $r.Exit, $verdict | Write-Host
}

Write-Host ""
Write-Host "$Config : $pass passed, $fail failed"
if ($fail -gt 0) {
    Write-Host "A 4 above is a FILE, not code: read $logs\MixConTestAuth.txt.err --"
    Write-Host "AuthArm refuses one reason at a time and names the one it stopped on."
}
Write-Host "logs in $logs"
exit $fail

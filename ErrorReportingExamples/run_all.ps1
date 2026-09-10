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
# run_all.ps1 -- build this tree and run what CAN be run unattended.
#
#   .\run_all.ps1                    # build + run Debug
#   .\run_all.ps1 -Config Release
#   .\run_all.ps1 -NoBuild
#   .\run_all.ps1 -NoDialog          # no message box appears on screen
#
# A MESSAGE BOX FLASHES UP during a default run. DialogOrLogFile raises a real
# modal dialog in a child process, because that is the property it exists to
# demonstrate, and then terminates the child that raised it. Nothing waits for a
# human and nothing is left behind. -NoDialog skips that leg, at the cost of the
# harness only showing half of its subject -- and it says so when it does.
#
# WHAT IS NOT RUN HERE, and why it is not a gap.
#
# Installing a SERVICE writes to HKLM and needs elevation, so the leg that hosts
# a hub under the SCM cannot be exercised here. A run_all that silently skipped it
# and printed SUCCESS would be claiming the interesting half had passed, so it
# does not claim it: the service leg is reported as NOT RUN, with the commands to
# run it by hand.
#
# What the two legs that DO run cover between them: the console leg takes a hub up
# and down with a diagnostic raised on its pump, and DialogOrLogFile covers the
# configuration file and both settings of it, including the host shape a service
# has -- no console, no standard error -- by re-executing itself to reach it.
#
# What IS run is the console leg, which exercises everything up to the point where
# the host shape starts to matter: the hub comes up, the pump reports itself, a
# deliberate P2Pevent is raised ON THE PUMP THREAD, the hub is asked to stop, and
# the process exits. That last part is the assertion -- a process that exits is a
# pump that was not blocked.
#
# The policy underneath is asserted properly, and on both platforms, by
# MscsUnitTests/p2p_servicedialog, which drives P2Pevent::TextOutputPolicy() over
# all sixteen combinations of its inputs. Run that for a verdict on the rule; run
# this for a demonstration of the shape.

[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')]
    [string] $Config = 'Debug',
    [switch] $NoBuild,
    [switch] $NoDialog,
    [int]    $TimeoutSeconds = 60
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$sln  = Join-Path $root 'ErrorReportingExamples(2026).sln'
$exe  = Join-Path $root ("out\x64\{0}\NTServiceEventLog.exe" -f $Config)
$exe2 = Join-Path $root ("out\x64\{0}\DialogOrLogFile.exe"   -f $Config)

function Write-Head([string] $text) {
    Write-Host ''
    Write-Host ('=' * 72)
    Write-Host $text
    Write-Host ('=' * 72)
}

# ---------------------------------------------------------------- build
if (-not $NoBuild) {
    Write-Head "Building $Config|x64"
    # No null-conditional (?.) and no ternary here: this has to run under Windows
    # PowerShell 5.1, which is what `powershell.exe` still is, and both are 7+.
    $found   = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    $msbuild = $null
    if ($found) { $msbuild = $found.Source }
    if (-not $msbuild) {
        $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path $vswhere) {
            $vs = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
            if ($vs) { $msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe' }
        }
    }
    if (-not $msbuild -or -not (Test-Path $msbuild)) {
        Write-Host 'FAIL: msbuild.exe not found. Open a Developer Prompt, or pass -NoBuild.'
        exit 1
    }

    & $msbuild $sln -p:Configuration=$Config -p:Platform=x64 -v:minimal -nologo
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAIL: build returned $LASTEXITCODE"
        # The post-build step fails deliberately when the kernel is missing, because
        # TargetCore is delay-loaded and the alternative is 0xC06D007E at startup.
        Write-Host "      If it complained about TargetCore.dll, build TargetCore first;"
        Write-Host "      this tree stages it from ..\..\bin\${Config}64."
        exit 1
    }
}

foreach ($e in @($exe, $exe2)) {
    if (-not (Test-Path $e)) {
        Write-Host "FAIL: $e not found."
        exit 1
    }
}

# ------------------------------------------------------- console leg
Write-Head 'NTServiceEventLog -Console  (the leg that runs unattended)'

# 'Q' on stdin quits the console menu. Without it this waits for a human, which
# is precisely the mistake the harness READMEs warn about.
#
# ErrorActionPreference is dropped to Continue for exactly this call. Under
# 'Stop', PowerShell promotes ANY line a native program writes to stderr into a
# terminating error - and this program writes its diagnostics to stderr by
# design, that being the whole subject of the tree. Left at 'Stop' the harness
# fails on its own correct output.
$out = $null
$rc  = 1
try {
    $ErrorActionPreference = 'Continue'
    $out = 'Q' | & $exe -Console 2>&1
    $rc  = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = 'Stop'
}
$out | ForEach-Object { '  ' + $_ }

$entered = ($out -match 'pump entering dispatch').Count -gt 0
$left    = ($out -match 'pump left dispatch').Count     -gt 0

Write-Host ''
Write-Host ("  exit code               : {0}   <- a pump that was never blocked" -f $rc)
Write-Host ("  pump entered dispatch   : {0}" -f $entered)
Write-Host ("  pump left dispatch      : {0}   (informational -- see below)" -f $left)

# THE VERDICT IS THE EXIT CODE, and deliberately not the 'left dispatch' line.
#
# The property being demonstrated is that the process TERMINATES: a diagnostic
# raised on the pump used to block that pump, CloseHub() waits on its pumps
# without a bound, and the process would then never exit at all. Exiting is the
# assertion.
#
# 'pump left dispatch' is printed by RunHub's tail on the SPAWNED pump thread,
# and in console mode the main thread has already returned from Run() by then.
# Whether that last line makes it out before the process is torn down is a race,
# and it was observed both ways on 2026-08-17. Gating on it would have produced
# an intermittent harness -- which is worse than a missing check, because a
# harness that fails at random gets ignored and then so does everything it says.
$consoleOk = ($rc -eq 0) -and $entered

# ----------------------------------------------- what the log received
Write-Head 'What reached the Windows application event log'

$events = Get-WinEvent -LogName Application -MaxEvents 60 -ErrorAction SilentlyContinue |
          Where-Object { $_.ProviderName -like 'P2Pmsg*' } |
          Select-Object -First 5
if (-not $events) {
    Write-Host '  (nothing from any P2Pmsg* provider)'
} else {
    foreach ($e in $events) {
        Write-Host ("  {0:HH:mm:ss}  {1}  Id={2}" -f $e.TimeCreated, $e.ProviderName, $e.Id)
        foreach ($p in $e.Properties) { Write-Host ('      ' + $p.Value) }
    }
}

$key = 'HKLM:\SYSTEM\CurrentControlSet\Services\EventLog\Application\P2PmsgErrorReportingExample'
if (Test-Path $key) {
    Write-Host ''
    Write-Host ('  source registered, EventMessageFile = ' + (Get-ItemProperty $key).EventMessageFile)
} else {
    Write-Host ''
    Write-Host '  source NOT registered, so the Event Viewer cannot format these.'
    Write-Host '  The text above is the raw insertion data, which is intact -- that is'
    Write-Host '  the difference registration makes, and all it makes.'
}

# -------------------------------------------------- DialogOrLogFile
Write-Head 'DialogOrLogFile  (the ErrToMessageBox switch, fully unattended)'
Write-Host '  Unlike the service leg, this one needs no elevation and no human: it'
Write-Host '  re-executes ITSELF as blind children to reach the host shape where the'
Write-Host '  dialog is reachable, watches for the dialog window, and terminates the'
Write-Host '  child that raised one. A message box does flash up during this.'
if ($NoDialog) {
    Write-Host '  -NoDialog was passed, so the leg that raises a real dialog is skipped'
    Write-Host '  and the harness will say so in its own verdict.'
}

# Same ErrorActionPreference dance as above, and for the same reason: this
# harness writes diagnostics to stderr by design.
$out2 = $null
$rc2  = 1
try {
    $ErrorActionPreference = 'Continue'
    if ($NoDialog) { $out2 = & $exe2 -nodialog 2>&1 } else { $out2 = & $exe2 2>&1 }
    $rc2 = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = 'Stop'
}
$out2 | ForEach-Object { '  ' + $_ }

# The verdict IS the exit code here, and that is not the compromise it was for
# the console leg above. This harness counts its own checks and returns 0 only
# when every one of them passed, so there is nothing to second-guess.
$dialogOk = ($rc2 -eq 0)
Write-Host ''
Write-Host ("  exit code               : {0}" -f $rc2)

# --------------------------------------------------------- service leg
Write-Head 'NTServiceEventLog as a real service  (NOT RUN -- needs elevation)'
Write-Host @"
  Installing a service writes to HKLM. From an ELEVATED prompt:

      $exe -Install
      sc start P2PmsgEventLogExample
      sc stop  P2PmsgEventLogExample
      $exe -Remove

  The assertion is that 'sc stop' returns promptly and the service reaches
  STOPPED rather than sitting in STOP_PENDING. See README.md.
"@

# ------------------------------------------------------------- verdict
Write-Head 'Verdict'
Write-Host ("  console leg       : {0}" -f ($(if ($consoleOk) {'PASS'} else {'FAIL'})))
Write-Host ("  DialogOrLogFile   : {0}{1}" -f ($(if ($dialogOk) {'PASS'} else {'FAIL'})),
                                              ($(if ($NoDialog) {' (partial -- -NoDialog)'} else {''})))
Write-Host  "  service leg       : NOT RUN (needs elevation)"

$failures = 0
if (-not $consoleOk) { $failures++ }
if (-not $dialogOk)  { $failures++ }
exit $failures

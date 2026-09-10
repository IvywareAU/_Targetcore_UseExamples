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
# build.ps1 -- compile the eleven C# harnesses and stage the runtime DLLs.
#
# WHY csc AND NOT A .csproj. This tree needs no NuGet package, no SDK, no
# targeting pack and no project system: every harness is `common\*.cs` plus one
# file, referencing nothing but mscorlib/System/System.Core. Driving Roslyn
# directly makes that visible and keeps the tree buildable on a machine with the
# C++ workload only -- which is what MSCS boxes usually have. If you would rather
# have VS integration, an SDK-style .csproj with <TargetFramework>net48</> and
# these same sources builds identically.
#
# TARGET: .NET Framework 4.8, x64. x64 is not optional -- TargetCom and the
# kernel underneath it are 64-bit, and AnyCPU would load a 32-bit CLR under
# WOW64 on some hosts and fail CoCreateInstance with a class-not-registered that
# has nothing to do with registration.
#
#   .\build.ps1
#   .\build.ps1 -Config Release
#   .\build.ps1 -Clean

[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')] [string] $Config = 'Debug',
    [switch] $Clean
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
# One output root, keyed by platform then configuration, as in every other tree
# here. This one is x64 by construction (see the TARGET note above), so the
# platform level has a single occupant rather than being merely decorative.
$bin  = Join-Path $here "out\x64\$Config"

if ($Clean -and (Test-Path $bin)) { Remove-Item -Recurse -Force $bin }

# ---- the compiler ---------------------------------------------------------
# Roslyn as shipped with Visual Studio; falls back to the in-box framework
# compiler, which is older but compiles these sources unchanged.
$csc = $null
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -property installationPath 2>$null | Select-Object -First 1
    if ($vs) {
        $candidate = Join-Path $vs 'MSBuild\Current\Bin\Roslyn\csc.exe'
        if (Test-Path $candidate) { $csc = $candidate }
    }
}
if (-not $csc) {
    $candidate = "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
    if (Test-Path $candidate) { $csc = $candidate }
}
if (-not $csc) { throw "no C# compiler found (looked for Roslyn in VS, then Framework64)" }

# ---- the reference assemblies ---------------------------------------------
$fw = "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319"
foreach ($asm in 'mscorlib.dll','System.dll','System.Core.dll') {
    if (-not (Test-Path (Join-Path $fw $asm))) { throw "missing $asm -- is .NET Framework 4.x installed?" }
}
$refs = @('mscorlib.dll','System.dll','System.Core.dll') | ForEach-Object { "/r:$(Join-Path $fw $_)" }

New-Item -ItemType Directory -Force -Path $bin | Out-Null

$common = @(
    (Join-Path $here 'common\TargetComInterop.cs')
    (Join-Path $here 'common\ComHarness.cs')
)

$harnesses = @(
    'WsaMeshTest', 'PipeMeshTest', 'DmxMeshTest', 'Com232MeshTest',
    'LocalInMemoryTest', 'PipeMsgMapTest', 'PipeMsgFactoryTest',
    'TwoConTest', 'RouteLoopbackTest', 'AlexTest', 'AlexInterop'
)

$debugFlags = if ($Config -eq 'Debug') { @('/debug+','/optimize-','/define:DEBUG;TRACE') }
              else                     { @('/debug:pdbonly','/optimize+','/define:TRACE') }

Write-Host "compiling $Config with $csc"
$failed = 0
foreach ($name in $harnesses) {
    $src = Join-Path $here "$name\$name.cs"
    if (-not (Test-Path $src)) { throw "missing $src" }
    $out = Join-Path $bin "$($name)Net.exe"

    & $csc /nologo /noconfig /nostdlib+ /platform:x64 /langversion:latest /warn:4 `
           /target:exe /utf8output @debugFlags @refs "/out:$out" @common $src
    if ($LASTEXITCODE -ne 0) { Write-Host "  FAILED $name"; $failed++ }
}
if ($failed) { throw "$failed harness(es) failed to compile" }

# ---- stage the runtime DLLs ------------------------------------------------
# Exactly what ComExamples\common\Com.props does in its post-build step.
# The exes reference none of these at compile time; the COM server and its
# dependencies simply have to be findable at run time, and run_all.ps1 registers
# the STAGED copy so this directory is self-contained.
$facade = Join-Path $here '..\..\TargetFacade'
$suffix = if ($Config -eq 'Debug') { 'd' } else { '' }
$core   = if ($Config -eq 'Debug') { Join-Path $here '..\..\bin\Debug64' } else { Join-Path $here '..\..\bin\Release64' }

# TargetCom and TargetFacade used to come from two separate directories under
# the facade tree; that repository now builds every project into one shared
# out\<Platform>\<Config>\ folder, so both are the same path.
$stage = @(
    (Join-Path $facade "out\x64\$Config\TargetCom.dll")
    (Join-Path $facade "out\x64\$Config\TargetFacade.dll")
    (Join-Path $core   "TargetCore.dll")
    (Join-Path $core   "Msgcore.dll")
)

foreach ($dll in $stage) {
    if (-not (Test-Path $dll)) {
        throw "missing $dll`nBuild TargetFacade\TargetFacade(2026).sln for $Config|x64 first."
    }
    Copy-Item $dll $bin -Force
}

Write-Host "$Config : $($harnesses.Count) harnesses + 4 staged DLLs -> $bin"

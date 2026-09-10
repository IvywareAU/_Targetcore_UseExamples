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
# build.ps1 -- compile the Java tree and stage the DLLs beside it.
#
#   .\build.ps1
#   .\build.ps1 -Config Release
#
# There is no pom.xml and no build tool. This tree has NO dependencies -- not on
# MSCS (it is handed no header, no .lib, no .tlb and no generated binding) and
# not on anything from Maven Central. javac over a source list is the whole
# build, and adding a build tool would only obscure that.
#
# Java 22+ is required, and not negotiable: java.lang.foreign left preview in 22.
# On 21 the same code needs --enable-preview on every javac AND java invocation,
# and the API differs.

[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')] [string] $Config = 'Debug'
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

# ---- locate a JDK 22+ ------------------------------------------------------
$javac = $null
if ($env:JAVA_HOME -and (Test-Path "$env:JAVA_HOME\bin\javac.exe")) {
    $javac = "$env:JAVA_HOME\bin\javac.exe"
} else {
    $cmd = Get-Command javac -ErrorAction SilentlyContinue
    if ($cmd) { $javac = $cmd.Source }
}
if (-not $javac) { throw "No javac found. Set JAVA_HOME to a JDK 22 or later." }

$verLine = (& $javac -version 2>&1) -join ' '
$major = 0
if ($verLine -match 'javac (\d+)') { $major = [int]$Matches[1] }
if ($major -lt 22) {
    throw "javac $major found at $javac, but java.lang.foreign needs JDK 22+. Set JAVA_HOME."
}
Write-Host "javac  : $javac ($verLine.Trim())"

# ---- stage the DLLs -------------------------------------------------------
#
# All three go into bin\<Config> together, and TargetFacade.dll is loaded from
# there by full path. Windows searches a loaded module's OWN directory for its
# dependencies, so staging the set side by side is what makes TargetCore.dll and
# Msgcore.dll resolve without touching PATH.
$src = Join-Path $here "..\..\TargetFacade\out\x64\$Config"
$bin = Join-Path $here "bin\$Config"

if (-not (Test-Path $src)) {
    throw "TargetFacade $Config build not found at $src. Build TargetFacade(2026).sln first."
}
New-Item -ItemType Directory -Force -Path $bin | Out-Null

foreach ($dll in @('TargetFacade.dll','TargetCore.dll','Msgcore.dll')) {
    $from = Join-Path $src $dll
    if (-not (Test-Path $from)) { throw "missing $dll in $src" }
    Copy-Item $from $bin -Force
}
Write-Host "staged : $bin  (TargetFacade.dll, TargetCore.dll, Msgcore.dll)"

# ---- compile ---------------------------------------------------------------
$out = Join-Path $here "out\$Config"
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force -Path $out | Out-Null

$sources = Get-ChildItem -Recurse -Filter *.java (Join-Path $here 'src') | ForEach-Object { $_.FullName }
Write-Host "compile: $($sources.Count) source files -> $out"

& $javac -Xlint:all -d $out @sources
if ($LASTEXITCODE -ne 0) { throw "javac failed" }

Write-Host "OK"

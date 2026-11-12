# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro
#
# Checks what building TacetOS needs on Windows and installs only what is
# missing, with winget. Linux and macOS: install-deps.sh.
#
# Usage: powershell -ExecutionPolicy Bypass -File scripts\install-deps.ps1 [-Check]
#   -Check  only report what is missing, install nothing.
#
# Qt has no winget package: Qt 6.8 is installed for this user only, with
# aqtinstall, in %USERPROFILE%\Qt. Windows on ARM64 is not handled yet.

param([switch]$Check)

$ErrorActionPreference = 'Stop'

$QtMin = [version]'6.8'
$QtFallback = '6.8.3'
$QtDir = Join-Path $env:USERPROFILE 'Qt'
$CMakeMin = [version]'3.21'

function Ok($text) { Write-Host "  [ok]      $text" }
function Missing($text) { Write-Host "  [missing] $text" }
function Has($cmd) { [bool](Get-Command $cmd -ErrorAction SilentlyContinue) }

if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') {
    Write-Host 'Windows on ARM64 is not handled by this script yet. See the README.'
    exit 1
}

Write-Host 'Checking the build tools:'

# C++ compiler: Visual Studio or its Build Tools, with the C++ workload.
$needCompiler = $true
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vs) { $needCompiler = $false }
}
if ($needCompiler) { Missing 'C++ compiler (Visual Studio Build Tools)' } else { Ok 'C++ compiler (Visual Studio)' }

$needCMake = $true
if (Has 'cmake') {
    $v = [version](((cmake --version)[0] -split ' ')[2] -replace '-.*', '')
    if ($v -ge $CMakeMin) { $needCMake = $false; Ok "CMake $v" }
    else { Missing "CMake $CMakeMin or newer (found $v)" }
} else { Missing "CMake $CMakeMin or newer" }

# Qt: one installed earlier by this script, or one already on the PATH.
$qtPrefix = $null
$found = Get-ChildItem -Path (Join-Path $QtDir '6.*\msvc2022_64') -Directory -ErrorAction SilentlyContinue |
    Where-Object { [version]($_.Parent.Name) -ge $QtMin } | Sort-Object { [version]$_.Parent.Name } | Select-Object -Last 1
if ($found) { $qtPrefix = $found.FullName }
elseif (Has 'qmake') {
    $v = qmake -query QT_VERSION
    if ($v -like '6.*' -and [version]$v -ge $QtMin) { $qtPrefix = qmake -query QT_INSTALL_PREFIX }
}
$needQt = -not $qtPrefix
if ($needQt) { Missing "Qt $QtMin or newer" } else { Ok "Qt in $qtPrefix" }

# The Microsoft Store puts a "python" stub on the PATH that only opens the Store.
$needPython = -not ((Has 'python') -and (& python --version 2>$null))
if ($needPython) { Missing 'Python 3 (tests, and the Qt installer)' } else { Ok 'Python 3' }
$needGit = -not (Has 'git')
if ($needGit) { Missing 'Git' } else { Ok 'Git' }

if (-not ($needCompiler -or $needCMake -or $needQt -or $needPython -or $needGit)) {
    Write-Host ''
    Write-Host 'Everything is there. Build with:'
    Write-Host "  cmake --preset debug -DCMAKE_PREFIX_PATH=`"$qtPrefix`""
    Write-Host '  cmake --build --preset debug'
    exit 0
}

$packages = @()
if ($needCMake) { $packages += 'Kitware.CMake' }
if ($needPython) { $packages += 'Python.Python.3.12' }
if ($needGit) { $packages += 'Git.Git' }

if ($Check) {
    Write-Host ''
    if ($needCompiler) { Write-Host 'Would install with winget: Microsoft.VisualStudio.2022.BuildTools (C++ workload)' }
    if ($packages) { Write-Host "Would install with winget: $($packages -join ' ')" }
    if ($needQt) { Write-Host "Would install Qt $QtFallback for this user in $QtDir" }
    exit 1
}

if (-not (Has 'winget')) {
    Write-Host ''
    Write-Host 'winget is needed (App Installer, from the Microsoft Store), then run this script again.'
    exit 1
}

Write-Host ''
if ($needCompiler) {
    Write-Host 'Installing the Visual Studio Build Tools with the C++ workload:'
    winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override '--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive --wait'
}
foreach ($p in $packages) {
    Write-Host "Installing $p"
    winget install --id $p -e
}
if ($packages) {
    # Pick up the new tools without opening a new window.
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
}

if ($needQt) {
    Write-Host "Installing Qt $QtFallback for this user in $QtDir"
    $venv = Join-Path $env:LOCALAPPDATA 'tacetos\aqt'
    python -m venv $venv
    & (Join-Path $venv 'Scripts\pip.exe') install --quiet --upgrade aqtinstall
    & (Join-Path $venv 'Scripts\aqt.exe') install-qt windows desktop $QtFallback win64_msvc2022_64 --outputdir $QtDir
    $qtPrefix = Join-Path $QtDir "$QtFallback\msvc2022_64"
}

Write-Host ''
Write-Host 'Done. Open a new terminal, then build with:'
Write-Host "  cmake --preset debug -DCMAKE_PREFIX_PATH=`"$qtPrefix`""
Write-Host '  cmake --build --preset debug'

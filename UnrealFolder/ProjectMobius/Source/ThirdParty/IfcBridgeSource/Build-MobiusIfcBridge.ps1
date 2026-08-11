# SPDX-License-Identifier: MIT
#
# Rebuilds Source/ThirdParty/MobiusIfcLibrary/install/{include,lib,bin} from the vendored IFC++
# source in this directory plus MobiusIfcBridge.h/.cpp.
#
# WHY THIS SCRIPT EXISTS: the repository .gitignore excludes *.dll and *.lib globally (there are
# zero tracked .dll/.lib files in the whole repo -- assimp and HDF5 are the same), so
# MobiusIfcBridge.dll / .lib are NOT committed. A fresh checkout has the source but not the
# binaries, and MobiusIfcLibrary.Build.cs hard-fails with a "run the CMake install step" message
# until this script has been run once. This is that step.
#
# Two CMake passes, both out-of-tree under Intermediate/ (gitignored, and the IfcPlusPlus build
# tree is ~310 MB with a 155 MB static lib -- it must never land in the repo):
#   1. IfcPlusPlus itself   -> IfcPlusPlus.lib (STATIC)
#   2. MobiusIfcBridge      -> MobiusIfcBridge.dll + .lib, installed into the UBT external module
#
# Toolset is pinned to 14.38.33130 -- the toolset UE 5.5 uses. The C ABI means a mismatch would
# probably work anyway, but the DLL links MSVCP140/VCRUNTIME140 and an ABI match removes an axis of
# divergence for free. See HANDOFF_IFC_2026-08-11.md section 13.0/13.3.
#
# Usage (from anywhere):
#   pwsh -File Build-MobiusIfcBridge.ps1
#   pwsh -File Build-MobiusIfcBridge.ps1 -Config Debug     # note: also produces MobiusIfcBridge.lib/.dll
#                                                          #       (no debug postfix -- Build.cs hardcodes one name)
#   pwsh -File Build-MobiusIfcBridge.ps1 -Clean            # wipe both build trees first

[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Config = 'Release',

    [string]$Generator = 'Visual Studio 17 2022',

    # Toolset UE 5.5 builds with. Verified present on this box 2026-08-11.
    [string]$Toolset = '14.38.33130',

    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$BridgeDir  = $PSScriptRoot                                             # .../Source/ThirdParty/IfcBridgeSource
$IfcppDir   = Join-Path $BridgeDir 'IfcPlusPlus'                         # vendored upstream checkout
$ThirdParty = Split-Path $BridgeDir -Parent                             # .../Source/ThirdParty
$InstallDir = Join-Path $ThirdParty 'MobiusIfcLibrary\install'           # what MobiusIfcLibrary.Build.cs reads
$ProjectDir = Split-Path (Split-Path $ThirdParty -Parent) -Parent        # .../UnrealFolder/ProjectMobius
$BuildRoot  = Join-Path $ProjectDir 'Intermediate\IfcBridgeBuild'
$IfcppBuild = Join-Path $BuildRoot 'ifcpp'
$ShimBuild  = Join-Path $BuildRoot 'shim'

if (-not (Test-Path (Join-Path $IfcppDir 'CMakeLists.txt'))) {
    throw "Vendored IFC++ source not found at $IfcppDir. Expected the upstream checkout (ifcquery/IfcPlusPlus @ 7b80900) vendored in-tree."
}

if ($Clean -and (Test-Path $BuildRoot)) {
    Write-Host "Cleaning $BuildRoot"
    Remove-Item -Recurse -Force $BuildRoot
}

function Invoke-CMake {
    param([string[]]$CMakeArgs)
    Write-Host "cmake $($CMakeArgs -join ' ')" -ForegroundColor DarkGray
    & cmake @CMakeArgs
    if ($LASTEXITCODE -ne 0) { throw "cmake failed with exit code $LASTEXITCODE" }
}

# ---------------------------------------------------------------------------------------------
# Pass 1 -- IfcPlusPlus static lib.
#
# BUILD_VIEWER_APPLICATION defaults ON upstream and pulls Qt5 + OpenSceneGraph; leaving it on
# makes configure die on missing Qt, which reads like an IFC++ failure and is not one.
# CMAKE_POLICY_VERSION_MINIMUM=3.5 is required under CMake 4.x (the repo still declares
# cmake_minimum_required(VERSION 3.6), which 4.x rejects outright). Both flags and the ~15
# harmless LNK4006 mz_zip_* duplicate-symbol warnings are documented in the handoff section 4.
# ---------------------------------------------------------------------------------------------
Invoke-CMake @(
    '-S', $IfcppDir, '-B', $IfcppBuild,
    '-G', $Generator, '-A', 'x64', '-T', "version=$Toolset",
    '-DBUILD_VIEWER_APPLICATION=OFF',
    '-DBUILD_CONSOLE_APPLICATION=OFF',
    '-DCMAKE_POLICY_VERSION_MINIMUM=3.5'
)
Invoke-CMake @('--build', $IfcppBuild, '--config', $Config, '--target', 'IfcPlusPlus', '--parallel')

$IfcppLibDir = Join-Path $IfcppBuild "IfcPlusPlus\$Config"
if (-not (Test-Path $IfcppLibDir)) { throw "IfcPlusPlus lib dir not produced: $IfcppLibDir" }

# ---------------------------------------------------------------------------------------------
# Pass 2 -- the shim DLL, in the CMakeLists' STANDALONE mode (points at the source tree for
# headers because IFC++'s own install() omits external/Carve entirely -- see CMakeLists.txt).
# ---------------------------------------------------------------------------------------------
Invoke-CMake @(
    '-S', $BridgeDir, '-B', $ShimBuild,
    '-G', $Generator, '-A', 'x64', '-T', "version=$Toolset",
    "-DMOBIUS_IFCPLUSPLUS_SOURCE_DIR=$IfcppDir",
    "-DMOBIUS_IFCPLUSPLUS_LIB_DIR=$IfcppLibDir",
    "-DCMAKE_INSTALL_PREFIX=$InstallDir"
)
Invoke-CMake @('--build', $ShimBuild, '--config', $Config, '--target', 'install', '--parallel')

foreach ($Expected in @(
        (Join-Path $InstallDir 'include\MobiusIfcBridge.h'),
        (Join-Path $InstallDir 'lib\MobiusIfcBridge.lib'),
        (Join-Path $InstallDir 'bin\MobiusIfcBridge.dll'))) {
    if (-not (Test-Path $Expected)) { throw "install step did not produce $Expected" }
}

Write-Host ''
Write-Host "MobiusIfcBridge installed to $InstallDir" -ForegroundColor Green
Get-ChildItem -Recurse -File $InstallDir | Select-Object FullName, Length | Format-Table -AutoSize

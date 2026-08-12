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
# Generator and toolset are DETECTED, not hardcoded. An earlier version of this script pinned
# -G "Visual Studio 17 2022" and -T version=14.38.33130, which fails outright on a machine with a
# different Visual Studio ("could not find any instance of Visual Studio") -- an error that names
# what we asked for and not what is installed. cmake\Resolve-MsvcToolchain.ps1 does the lookup and
# prints what it picked. 14.38.33130 is still PREFERRED, because it is the toolset UE 5.5 builds
# with: the C ABI at the shim boundary means a mismatch would probably work anyway, but the DLL
# links MSVCP140/VCRUNTIME140 and an exact match removes an axis of divergence for free. See
# HANDOFF_IFC_2026-08-11.md section 13.0/13.3.
#
# Usage (from anywhere). There is no `pwsh` requirement -- Windows PowerShell 5.1 runs this fine:
#   powershell -NoProfile -ExecutionPolicy Bypass -File Build-MobiusIfcBridge.ps1
#   powershell -File Build-MobiusIfcBridge.ps1 -Config Debug   # note: also produces MobiusIfcBridge.lib/.dll
#                                                              #       (no debug postfix -- Build.cs hardcodes one name)
#   powershell -File Build-MobiusIfcBridge.ps1 -Clean          # wipe both build trees first
#
# Normally you do not run this by hand at all: the superbuild drives it.
#   ..\..\..\Setup-Superbuild.ps1

[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Config = 'Release',

    # '' or 'auto' -> detect the installed Visual Studio. Anything else is used verbatim.
    [AllowEmptyString()]
    [string]$Generator = 'auto',

    # 'auto'         -> prefer 14.38.33130 (UE 5.5's), else newest installed.
    # 'none' (or '') -> pass no -T at all and use the generator default. Prefer the word:
    #                   `powershell -File ... -Toolset ""` silently drops the empty argument and
    #                   fails with "Missing an argument for parameter 'Toolset'".
    [AllowEmptyString()]
    [string]$Toolset = 'auto',

    # IfcPlusPlus declares cmake_minimum_required(VERSION 3.6), which modern CMake rejects outright.
    # Raise this if your CMake has removed compatibility below 3.10.
    [string]$PolicyMinimum = '3.5',

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

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "cmake was not found on PATH. Install CMake 3.21 or newer (https://cmake.org/download/) and reopen your shell."
}

# Resolve generator/toolset. Shared with Setup-Superbuild.ps1 so both entry points agree.
$Resolver = Join-Path $ProjectDir 'cmake\Resolve-MsvcToolchain.ps1'
if (-not (Test-Path $Resolver)) {
    throw "Toolchain resolver not found at $Resolver. It ships alongside CMakeLists.txt; restore it or pass -Generator and -Toolset explicitly."
}
. $Resolver

$Generator = Resolve-MobiusGenerator -Requested $Generator
$Toolset   = Resolve-MobiusToolset   -Requested $Toolset

$GeneratorArgs = @('-G', $Generator, '-A', 'x64')
$ToolsetArgument = Convert-MobiusToolsetToArgument -Toolset $Toolset
if ($ToolsetArgument) { $GeneratorArgs += @('-T', $ToolsetArgument) }

Write-Host "MobiusIfcBridge: $Config | generator '$Generator' | toolset '$(if ($Toolset) { $Toolset } else { '<generator default>' })'" -ForegroundColor Cyan

if ($Clean -and (Test-Path $BuildRoot)) {
    Write-Host "Cleaning $BuildRoot"
    Remove-Item -Recurse -Force $BuildRoot
}

# A build tree configured for a different generator cannot be reused -- CMake refuses with
# "generator ... does not match the generator used previously". Detect it and reconfigure from
# scratch rather than making the caller work out that they need -Clean.
function Reset-StaleBuildTree {
    param([string]$BuildDir, [string]$ExpectedGenerator)
    $Cache = Join-Path $BuildDir 'CMakeCache.txt'
    if (-not (Test-Path $Cache)) { return }

    $Reason = $null
    $CachedGenerator = (Select-String -Path $Cache -Pattern '^CMAKE_GENERATOR:INTERNAL=(.*)$' | Select-Object -First 1)
    if ($CachedGenerator -and $CachedGenerator.Matches[0].Groups[1].Value -ne $ExpectedGenerator) {
        $Reason = "generator changed ('$($CachedGenerator.Matches[0].Groups[1].Value)' -> '$ExpectedGenerator')"
    }
    $CachedHome = (Select-String -Path $Cache -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$' | Select-Object -First 1)
    if (-not $Reason -and $CachedHome) {
        # NOT $Home -- that is a read-only PowerShell automatic variable and assigning it throws.
        $CachedSourceDir = $CachedHome.Matches[0].Groups[1].Value
        if (-not (Test-Path $CachedSourceDir)) {
            $Reason = "cached source directory '$CachedSourceDir' no longer exists (build tree copied from another machine)"
        }
    }

    if ($Reason) {
        Write-Warning "Discarding stale build tree ${BuildDir}: $Reason"
        Remove-Item -Recurse -Force $BuildDir
    }
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
# CMAKE_POLICY_VERSION_MINIMUM is required under CMake 4.x (the repo still declares
# cmake_minimum_required(VERSION 3.6), which 4.x rejects outright); see -PolicyMinimum. Both flags
# and the ~15 harmless LNK4006 mz_zip_* duplicate-symbol warnings are documented in the handoff
# section 4.
# ---------------------------------------------------------------------------------------------
Reset-StaleBuildTree -BuildDir $IfcppBuild -ExpectedGenerator $Generator
Invoke-CMake (@(
    '-S', $IfcppDir, '-B', $IfcppBuild) + $GeneratorArgs + @(
    '-DBUILD_VIEWER_APPLICATION=OFF',
    '-DBUILD_CONSOLE_APPLICATION=OFF',
    "-DCMAKE_POLICY_VERSION_MINIMUM=$PolicyMinimum"
))
Invoke-CMake @('--build', $IfcppBuild, '--config', $Config, '--target', 'IfcPlusPlus', '--parallel')

$IfcppLibDir = Join-Path $IfcppBuild "IfcPlusPlus\$Config"
if (-not (Test-Path $IfcppLibDir)) { throw "IfcPlusPlus lib dir not produced: $IfcppLibDir" }

# ---------------------------------------------------------------------------------------------
# Pass 2 -- the shim DLL, in the CMakeLists' STANDALONE mode (points at the source tree for
# headers because IFC++'s own install() omits external/Carve entirely -- see CMakeLists.txt).
# ---------------------------------------------------------------------------------------------
Reset-StaleBuildTree -BuildDir $ShimBuild -ExpectedGenerator $Generator
Invoke-CMake (@(
    '-S', $BridgeDir, '-B', $ShimBuild) + $GeneratorArgs + @(
    "-DMOBIUS_IFCPLUSPLUS_SOURCE_DIR=$IfcppDir",
    "-DMOBIUS_IFCPLUSPLUS_LIB_DIR=$IfcppLibDir",
    "-DCMAKE_INSTALL_PREFIX=$InstallDir"
))
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

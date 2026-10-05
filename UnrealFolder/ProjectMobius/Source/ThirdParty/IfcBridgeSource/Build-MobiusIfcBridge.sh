#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# macOS/Unix counterpart of Build-MobiusIfcBridge.ps1. Rebuilds
# Source/ThirdParty/MobiusIfcLibrary/install/{include,bin} from the vendored IFC++ source in this
# directory plus MobiusIfcBridge.h/.cpp. The superbuild drives it (CMakeLists.txt runs this on
# APPLE the same way it runs the .ps1 on WIN32); you rarely call it by hand.
#
# WHY IT EXISTS: the repo .gitignore excludes built libraries, so the shim is never committed. A
# fresh checkout has the source but not libMobiusIfcBridge.dylib, and MobiusIfcLibrary.Build.cs
# fails with a "run the CMake install step" message until this has run once. This is that step.
#
# Two CMake passes, both out-of-tree under Intermediate/IfcBridgeBuild (gitignored, ~300 MB):
#   1. IfcPlusPlus itself  -> libIfcPlusPlus.a (STATIC)
#   2. MobiusIfcBridge     -> libMobiusIfcBridge.dylib, installed into the UBT external module
#
# Unlike the Windows path there is no Visual Studio generator or MSVC toolset to resolve: the
# single-config default generator (Unix Makefiles) plus -DCMAKE_BUILD_TYPE is correct on macOS,
# and clang needs none of MSVC's /bigobj, /MP, Bcrypt.lib or delay-load machinery (all guarded
# behind if(WIN32)/if(MSVC) in the vendored CMakeLists).
#
# Usage (from anywhere):
#   bash Build-MobiusIfcBridge.sh [--config Release|Debug] [--policy-min 3.5] [--clean]
set -euo pipefail

CONFIG="Release"
POLICY_MIN="3.5"
CLEAN=0
while [ $# -gt 0 ]; do
  case "$1" in
    --config)     CONFIG="${2:?--config needs a value}"; shift 2 ;;
    --policy-min) POLICY_MIN="${2:?--policy-min needs a value}"; shift 2 ;;
    --clean)      CLEAN=1; shift ;;
    *) echo "Build-MobiusIfcBridge.sh: unknown argument '$1'" >&2; exit 2 ;;
  esac
done

# Resolve paths from this script's own location, mirroring the .ps1's $PSScriptRoot layout.
BRIDGE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"        # .../Source/ThirdParty/IfcBridgeSource
IFCPP="$BRIDGE/IfcPlusPlus"                                    # vendored upstream checkout
THIRDPARTY="$(dirname "$BRIDGE")"                              # .../Source/ThirdParty
PROJECT_DIR="$(dirname "$(dirname "$THIRDPARTY")")"            # .../UnrealFolder/ProjectMobius
INSTALL_DIR="$THIRDPARTY/MobiusIfcLibrary/install"            # what MobiusIfcLibrary.Build.cs reads
BUILD_ROOT="$PROJECT_DIR/Intermediate/IfcBridgeBuild"
IFCPP_BUILD="$BUILD_ROOT/ifcpp"
SHIM_BUILD="$BUILD_ROOT/shim"

if [ ! -f "$IFCPP/CMakeLists.txt" ]; then
  echo "Vendored IFC++ source not found at $IFCPP (expected ifcquery/IfcPlusPlus vendored in-tree)." >&2
  exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake not found on PATH. Install CMake 3.21+ (https://cmake.org/download/) and reopen your shell." >&2
  exit 1
fi

if [ "$CLEAN" -eq 1 ] && [ -d "$BUILD_ROOT" ]; then
  echo "Cleaning $BUILD_ROOT"
  rm -rf "$BUILD_ROOT"
fi

# A build tree whose cached source directory no longer exists was copied from another machine and
# cannot be reused; CMake would fail confusingly. Discard it rather than make the caller pass --clean.
reset_stale_tree() {
  local dir="$1" cache="$1/CMakeCache.txt" home
  [ -f "$cache" ] || return 0
  home="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$cache" | head -1)"
  if [ -n "$home" ] && [ ! -d "$home" ]; then
    echo "Discarding stale build tree $dir (cached source '$home' no longer exists)"
    rm -rf "$dir"
  fi
}

echo "MobiusIfcBridge (macOS): config=$CONFIG  arch=$(uname -m)  cmake=$(cmake --version | head -1 | awk '{print $3}')"

# --------------------------------------------------------------------------------------------
# Pass 1 -- IfcPlusPlus static lib. BUILD_VIEWER_APPLICATION pulls Qt5 + OpenSceneGraph (off);
# BUILD_CONSOLE_APPLICATION builds examples we do not need (off). CMAKE_POLICY_VERSION_MINIMUM is
# required under CMake 4.x, which rejects the repo's cmake_minimum_required(VERSION 3.6) outright.
# --------------------------------------------------------------------------------------------
reset_stale_tree "$IFCPP_BUILD"
cmake -S "$IFCPP" -B "$IFCPP_BUILD" \
  -DCMAKE_BUILD_TYPE="$CONFIG" \
  -DBUILD_VIEWER_APPLICATION=OFF \
  -DBUILD_CONSOLE_APPLICATION=OFF \
  -DCMAKE_POLICY_VERSION_MINIMUM="$POLICY_MIN"
cmake --build "$IFCPP_BUILD" --config "$CONFIG" --target IfcPlusPlus --parallel

IFCPP_LIB="$(find "$IFCPP_BUILD" -name 'libIfcPlusPlus*.a' | head -1)"
if [ -z "$IFCPP_LIB" ]; then
  echo "IfcPlusPlus static lib was not produced under $IFCPP_BUILD" >&2
  exit 1
fi
IFCPP_LIB_DIR="$(dirname "$IFCPP_LIB")"
echo "IfcPlusPlus: $IFCPP_LIB"

# --------------------------------------------------------------------------------------------
# Pass 2 -- the shim, in the CMakeLists' STANDALONE mode (points at the source tree for headers
# because IFC++'s own install() omits external/Carve entirely -- see CMakeLists.txt). Force the
# dylib's install_name to @rpath so MobiusIfcLibrary.Build.cs can stage it next to the binary and
# have dyld resolve it, matching UE_AssimpLibrary's macOS handling.
# --------------------------------------------------------------------------------------------
reset_stale_tree "$SHIM_BUILD"
cmake -S "$BRIDGE" -B "$SHIM_BUILD" \
  -DCMAKE_BUILD_TYPE="$CONFIG" \
  -DMOBIUS_IFCPLUSPLUS_SOURCE_DIR="$IFCPP" \
  -DMOBIUS_IFCPLUSPLUS_LIB_DIR="$IFCPP_LIB_DIR" \
  -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
  -DCMAKE_INSTALL_NAME_DIR="@rpath" \
  -DCMAKE_POLICY_VERSION_MINIMUM="$POLICY_MIN"
cmake --build "$SHIM_BUILD" --config "$CONFIG" --target install --parallel

for expected in \
  "$INSTALL_DIR/include/MobiusIfcBridge.h" \
  "$INSTALL_DIR/bin/libMobiusIfcBridge.dylib"; do
  if [ ! -f "$expected" ]; then
    echo "install step did not produce $expected" >&2
    exit 1
  fi
done

echo ""
echo "MobiusIfcBridge installed to $INSTALL_DIR"
find "$INSTALL_DIR" -type f | sort

cmake_minimum_required(VERSION 3.21)
project(MobiusSuperbuild LANGUAGES C CXX)

# =============================================================================================
# Project Mobius superbuild
# =============================================================================================
#
# Builds the three vendored native dependencies into the exact on-disk layouts that
# UnrealBuildTool's external modules expect, then stages their runtime binaries:
#
#   1. Assimp          -> Plugins/UE4_Assimp/Source/ThirdParty/UE_AssimpLibrary/assimp/{include,lib,bin}
#                         staged to Plugins/UE4_Assimp/Binaries/<Platform>
#   2. HDF5            -> Plugins/MobiusDataImporter/Source/ThirdParty/hdf5-2.0.0/install/{include,lib}
#   3. MobiusIfcBridge -> Source/ThirdParty/MobiusIfcLibrary/install/{include,lib,bin}   (Win64 only)
#
# If any of these has not been built, UBT fails during the *Unreal* build with a message like:
#
#   Unable to instantiate module 'MobiusIfcLibrary': MobiusIfcLibrary lib dir not found:
#   ...\Source\ThirdParty\MobiusIfcLibrary\install\lib. Run the CMake install step.
#
# "the CMake install step" is this file. None of the .lib/.dll outputs are committed (the repo
# .gitignore excludes *.lib and *.dll globally), so a fresh checkout MUST run the superbuild once
# before opening the .uproject.
#
# ---------------------------------------------------------------------------------------------
# USAGE
# ---------------------------------------------------------------------------------------------
#
# Any platform:
#
#   python superbuild.py
#
# or plain CMake, which is equally supported:
#
#   cmake --fresh -S . -B _superbuild -DCMAKE_BUILD_TYPE=Release
#   cmake --build _superbuild --config Release --parallel
#
# `--fresh` (CMake 3.24+) discards the existing cache and reconfigures -- the fix for a build tree
# copied in from another machine, which carries an absolute CMAKE_HOME_DIRECTORY and makes CMake
# refuse before it ever reaches a compiler. Drop it for an ordinary incremental build.
#
# Note the absence of -G: CMake picks the newest installed Visual Studio itself. Hardcoding
# -G "Visual Studio 17 2022" is what used to break this project on any machine with a different
# Visual Studio, because the resulting error never says what IS installed.
#
# superbuild.py adds what CMake cannot do alone: it pins the MSVC toolset to 14.38.33130 when
# present (UE 5.5's own), explains a missing/unsupported Visual Studio instead of failing blankly,
# discards an unusable build tree and says why, and prints a per-dependency summary at the end.
# =============================================================================================

# ---------------------------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------------------------
option(SUPERBUILD_ENABLE_TESTS "Generate ctest staging checks for the built dependencies" ON)
option(SUPERBUILD_RUN_TESTS    "Run those checks automatically at the end of a build" ON)
option(SUPERBUILD_TESTS_ARE_FATAL "A failing staging check fails the build (rather than printing a note)" ON)
set(SUPERBUILD_CTEST_CONFIG "Release" CACHE STRING "Configuration ctest is invoked with on multi-config generators")

option(SUPERBUILD_BUILD_ASSIMP "Build and stage Assimp" ON)
option(SUPERBUILD_BUILD_HDF5   "Build HDF5" ON)
option(SUPERBUILD_BUILD_IFC    "Build the MobiusIfcBridge DLL (IFC++ shim). Win64/MSVC only." ON)

option(SUPERBUILD_PRUNE_ASSIMP_VARIANTS
  "Delete previously-installed assimp-vcNNN-mt.* before installing, so exactly one toolset variant remains"
  ON)

set(SUPERBUILD_IFC_CONFIG "Release" CACHE STRING "Configuration used for the IFC++ / MobiusIfcBridge build (Release or Debug)")
set(SUPERBUILD_IFC_POLICY_MIN "3.5" CACHE STRING
  "CMAKE_POLICY_VERSION_MINIMUM passed to the IFC++ build. IfcPlusPlus declares cmake_minimum_required(3.6), which modern CMake rejects; raise this if your CMake removes compatibility below 3.10.")
option(SUPERBUILD_IFC_FORCE "Rebuild MobiusIfcBridge even if install/lib and install/bin already exist" OFF)

# Optional Unreal automation hooks (off by default; require user-provided commands/paths)
option(SUPERBUILD_ENABLE_UE_AUTOMATION "Add a ctest that runs Unreal automation (UnrealEditor-Cmd)" OFF)
set(UE_AUTOMATION_EXE "" CACHE STRING "Path to UnrealEditor-Cmd.exe (or platform equivalent)")
set(UE_AUTOMATION_ARGS "" CACHE STRING "Arguments for Unreal automation (e.g. -run=Automation -Test=All -unattended -nop4 -log)")
option(SUPERBUILD_ENABLE_UE_GENBUILD "Add ctests that run GenerateProjectFiles and build the editor target" OFF)
set(UE_GENERATE_FILES_EXE "" CACHE STRING "Path to GenerateProjectFiles.bat/sh")
set(UE_GENERATE_FILES_ARGS "" CACHE STRING "Args for GenerateProjectFiles (e.g. -project=ProjectMobius.uproject -game -engine)")
set(UE_BUILD_EXE "" CACHE STRING "Path to the UE build helper (e.g. Engine/Build/BatchFiles/Build.bat)")
set(UE_BUILD_ARGS "" CACHE STRING "Args for UE build (e.g. ProjectMobiusEditor Win64 Development -Project=ProjectMobius.uproject -WaitMutex)")

if(SUPERBUILD_ENABLE_TESTS)
  enable_testing()
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/Testing")
endif()

# ---------------------------------------------------------------------------------------------
# Global build settings
# ---------------------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# NOTE: this superbuild has no install() rules of its own -- every sub-project is given an explicit
# CMAKE_INSTALL_PREFIX pointing into the Unreal source tree, because that is where UBT reads them
# from. There is deliberately no top-level install prefix to configure. `cmake --install` on this
# build tree does nothing, by design.

include(ExternalProject)

# ---------------------------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------------------------
set(UE_ROOT   "${CMAKE_SOURCE_DIR}")
set(REPO_ROOT "${CMAKE_SOURCE_DIR}/../..")
set(REPO_DOCS_SRC_DIR "${REPO_ROOT}/docs")
set(BUILD_DOCS_DIR "${UE_ROOT}/BuildDocs")
set(BUILD_DOCS_MIRROR_DIR "${BUILD_DOCS_DIR}/Docs")
set(BUILD_DOCS_SYNC_SCRIPT "${CMAKE_BINARY_DIR}/SyncBuildDocs.cmake")

set(ASSIMP_SRC "${UE_ROOT}/Plugins/UE4_Assimp/Source/ThirdParty/UE_AssimpLibrary/assimp")
set(MOBIUS_DATA_IMPORTER_PLUGIN_DIR "${UE_ROOT}/Plugins/MobiusDataImporter")
set(HDF5_SRC "${MOBIUS_DATA_IMPORTER_PLUGIN_DIR}/Source/ThirdParty/hdf5-2.0.0")

set(IFC_BRIDGE_SRC  "${UE_ROOT}/Source/ThirdParty/IfcBridgeSource")
set(IFC_INSTALL_DIR "${UE_ROOT}/Source/ThirdParty/MobiusIfcLibrary/install")

# --- Platform output folders Unreal expects ---
if(WIN32)
  set(PLAT Win64)
elseif(APPLE)
  set(PLAT Mac)
else()
  set(PLAT Linux)
endif()

set(ASSIMP_STAGE_DIR "${UE_ROOT}/Plugins/UE4_Assimp/Binaries/${PLAT}")

# ---------------------------------------------------------------------------------------------
# Fail early, and readably, on a missing prerequisite.
#
# Every check below fails LATER and much less legibly if left to the sub-build: a missing vendored
# source tree surfaces as "The source directory ... does not appear to contain CMakeLists.txt" from
# inside an ExternalProject step, several hundred lines into a build log.
# ---------------------------------------------------------------------------------------------
function(mobius_require_dir Description PathToCheck Remedy)
  if(NOT EXISTS "${PathToCheck}")
    message(FATAL_ERROR
      "${Description} not found:\n  ${PathToCheck}\n${Remedy}")
  endif()
endfunction()

if(SUPERBUILD_BUILD_ASSIMP)
  mobius_require_dir("Vendored Assimp source" "${ASSIMP_SRC}/CMakeLists.txt"
    "This is vendored in-tree. A partial checkout or a git-lfs/sparse-checkout skip is the usual cause.")
endif()
if(SUPERBUILD_BUILD_HDF5)
  mobius_require_dir("Vendored HDF5 source" "${HDF5_SRC}/CMakeLists.txt"
    "This is vendored in-tree. A partial checkout is the usual cause.")
endif()
mobius_require_dir("Canonical documentation directory" "${REPO_DOCS_SRC_DIR}"
  "Expected the repository root two levels above this file (<repo>/docs). If you moved CMakeLists.txt, fix REPO_ROOT.")

# Report the toolchain we actually resolved. On Windows the single most common cause of a broken
# superbuild is a generator that does not match the installed Visual Studio -- surface it up front
# instead of leaving it to a sub-build failure.
message(STATUS "Mobius superbuild -- generator: ${CMAKE_GENERATOR}")
if(CMAKE_GENERATOR_PLATFORM)
  message(STATUS "Mobius superbuild -- platform:  ${CMAKE_GENERATOR_PLATFORM}")
endif()
if(CMAKE_GENERATOR_TOOLSET)
  message(STATUS "Mobius superbuild -- toolset:   ${CMAKE_GENERATOR_TOOLSET}")
elseif(MSVC)
  # -T needs the platform toolset name as well as the version: `-T version=14.38.33130` alone is
  # rejected by the VS 2026 generator, which pairs it with its own default v145.
  message(STATUS "Mobius superbuild -- toolset:   <default> (MSVC ${MSVC_VERSION}). "
                 "UE 5.5 builds with 14.38.33130; pass -T v143,version=14.38.33130 to match its ABI exactly, "
                 "or use superbuild.py which resolves this for you.")
endif()
message(STATUS "Mobius superbuild -- build tree: ${CMAKE_BINARY_DIR}")

configure_file(
  "${UE_ROOT}/cmake/SyncBuildDocs.cmake.in"
  "${BUILD_DOCS_SYNC_SCRIPT}"
  @ONLY
)

add_custom_target(sync_builddocs ALL
  COMMAND ${CMAKE_COMMAND} -P "${BUILD_DOCS_SYNC_SCRIPT}"
  COMMENT "Syncing repo-level docs into ${BUILD_DOCS_MIRROR_DIR}"
)

# =============================================================================================
# 1) Assimp -- built shared, installed into the plugin's ThirdParty layout
# =============================================================================================
if(SUPERBUILD_BUILD_ASSIMP)
  set(ASSIMP_BUILD "${CMAKE_BINARY_DIR}/assimp-build")

  # Install directly where UE modules expect it (this is the vendored source tree itself --
  # UE_AssimpLibrary.Build.cs reads assimp/{include,lib/<Plat>,bin/<Plat>}).
  set(ASSIMP_INST "${ASSIMP_SRC}")

  # Assimp tests are unconditionally disabled -- the upstream test/ directory (which contained
  # CC-BY-SA content) has been removed from this repository.
  set(_assimp_build_tests OFF)

  set(_assimp_cmake_args
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED_LIBS=ON
    -DASSIMP_BUILD_TESTS=${_assimp_build_tests}
    -DASSIMP_BUILD_ASSIMP_TOOLS=OFF
    -DASSIMP_BUILD_SAMPLES=OFF
    -DCMAKE_INSTALL_PREFIX=${ASSIMP_INST}
  )

  # Per-platform install subdirs that match UE_AssimpLibrary.Build.cs
  if(WIN32)
    list(APPEND _assimp_cmake_args
      -DASSIMP_BUILD_ZLIB=ON
      -DASSIMP_BIN_INSTALL_DIR=bin/Win64
      -DASSIMP_LIB_INSTALL_DIR=lib/Win64
      -DASSIMP_INCLUDE_INSTALL_DIR=include
    )
    set(ASSIMP_RUNTIME_SRC_DIR "${ASSIMP_INST}/bin/Win64")
    set(ASSIMP_RUNTIME_PATTERNS "\"assimp*.dll\"")
  elseif(APPLE)
    # Build Assimp as arm64 (Apple Silicon) and install libs under lib/Mac so the UE_Assimp
    # plugin's Build.cs and SDK checks can find them without hacks. Assimp on UNIX derives
    # ASSIMP_LIB_INSTALL_DIR from CMAKE_INSTALL_LIBDIR, so force that instead.
    list(APPEND _assimp_cmake_args
      -DASSIMP_BUILD_ZLIB=OFF
      -DCMAKE_MACOSX_RPATH=ON
      -DCMAKE_INSTALL_NAME_DIR=@rpath
      -DCMAKE_OSX_ARCHITECTURES=arm64
      -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
      -DCMAKE_INSTALL_LIBDIR=lib/Mac
      -DASSIMP_INCLUDE_INSTALL_DIR=include
    )
    set(ASSIMP_RUNTIME_SRC_DIR "${ASSIMP_INST}/lib/Mac")
    set(ASSIMP_RUNTIME_PATTERNS "\"libassimp*.dylib\"")
  else()
    list(APPEND _assimp_cmake_args
      -DASSIMP_BUILD_ZLIB=OFF
      -DASSIMP_BIN_INSTALL_DIR=bin/Linux
      -DASSIMP_LIB_INSTALL_DIR=lib/Linux
      -DASSIMP_INCLUDE_INSTALL_DIR=include
    )
    set(ASSIMP_RUNTIME_SRC_DIR "${ASSIMP_INST}/lib/Linux")
    set(ASSIMP_RUNTIME_PATTERNS "\"libassimp*.so*\"")
  endif()

  ExternalProject_Add(assimp_ep
    SOURCE_DIR  "${ASSIMP_SRC}"
    BINARY_DIR  "${ASSIMP_BUILD}"
    INSTALL_DIR "${ASSIMP_INST}"
    CMAKE_ARGS  ${_assimp_cmake_args}
    BUILD_COMMAND   ${CMAKE_COMMAND} --build . --config Release --parallel
    INSTALL_COMMAND ${CMAKE_COMMAND} --build . --target install --config Release --parallel
    UPDATE_DISCONNECTED 1
    USES_TERMINAL_BUILD 1
  )

  # Drop stale toolset variants so the install below leaves exactly one assimp-vcNNN-mt set.
  # See cmake/PruneAssimpVariants.cmake.in for why UE_AssimpLibrary.Build.cs needs this.
  if(WIN32 AND SUPERBUILD_PRUNE_ASSIMP_VARIANTS)
    # SPACE-separated, not semicolon-separated. `set(_x "a";"b")` leaves the SECOND element as the
    # 6-character string `"b"` -- quotes included -- so every glob built from it silently matches
    # nothing. That is how an earlier version of this pruned bin/Win64 but not lib/Win64, leaving a
    # vc143 import lib beside a vc145 DLL: exactly the mismatch the prune exists to prevent.
    set(ASSIMP_PRUNE_DIRS "\"${ASSIMP_INST}/bin/Win64\" \"${ASSIMP_INST}/lib/Win64\"")
    set(ASSIMP_PRUNE_PATTERNS "\"assimp-vc*-mt.dll\" \"assimp-vc*-mt.lib\" \"assimp-vc*-mt.pdb\" \"assimp-vc*-mt.exp\"")
    set(ASSIMP_PRUNE_SCRIPT "${CMAKE_BINARY_DIR}/PruneAssimpVariants.cmake")
    configure_file("${UE_ROOT}/cmake/PruneAssimpVariants.cmake.in" "${ASSIMP_PRUNE_SCRIPT}" @ONLY)

    ExternalProject_Add_Step(assimp_ep prune_stale_variants
      COMMAND ${CMAKE_COMMAND} -P "${ASSIMP_PRUNE_SCRIPT}"
      COMMENT "Removing previously-installed Assimp toolset variants"
      DEPENDEES build
      DEPENDERS install
    )
  endif()

  # Ensure generated headers (revision.h, config.h) exist under include/assimp after install.
  # Upstream generates them in the build tree; copy them into our install include tree.
  ExternalProject_Add_Step(assimp_ep install_generated_headers
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ASSIMP_INST}/include/assimp"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ASSIMP_BUILD}/include/assimp/revision.h" "${ASSIMP_INST}/include/assimp/revision.h"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ASSIMP_BUILD}/include/assimp/config.h" "${ASSIMP_INST}/include/assimp/config.h"
    DEPENDEES install
    WORKING_DIRECTORY "${ASSIMP_BUILD}"
  )

  # Stage the Assimp runtime into the plugin's Binaries/<Platform> for editor runs.
  #
  # This target used to `rm -rf` the stage directory first. It must not: UnrealBuildTool writes the
  # plugin's OWN output (UnrealEditor-UE_Assimp.dll/.pdb, UnrealEditor.modules, hot-reload -000N
  # variants) into that same folder, so the wipe deleted the compiled UE plugin on every superbuild
  # and failed outright when the editor held those files open. The staging script copies only
  # assimp's own binaries, by pattern.
  set(ASSIMP_STAGE_SCRIPT "${CMAKE_BINARY_DIR}/StageAssimpRuntime.cmake")
  configure_file("${UE_ROOT}/cmake/StageAssimpRuntime.cmake.in" "${ASSIMP_STAGE_SCRIPT}" @ONLY)

  add_custom_target(assimp_stage ALL
    COMMAND ${CMAKE_COMMAND} -P "${ASSIMP_STAGE_SCRIPT}"
    COMMENT "Staging Assimp runtime to ${ASSIMP_STAGE_DIR}"
  )
  add_dependencies(assimp_stage assimp_ep)
endif()

# =============================================================================================
# 2) HDF5 -- static, installed into the MobiusDataImporter ThirdParty layout
# =============================================================================================
if(SUPERBUILD_BUILD_HDF5)
  set(HDF5_BUILD "${CMAKE_BINARY_DIR}/hdf5-build")
  set(HDF5_INST "${HDF5_SRC}/install")

  set(_hdf5_cmake_args
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED_LIBS=OFF
    -DBUILD_STATIC_LIBS=ON
    -DHDF5_BUILD_TOOLS=OFF
    -DHDF5_BUILD_EXAMPLES=OFF
    -DBUILD_TESTING=OFF
    -DHDF5_BUILD_CPP_LIB=OFF
    -DHDF5_BUILD_FORTRAN=OFF
    -DHDF5_BUILD_JAVA=OFF
    -DHDF5_BUILD_HL_LIB=ON
    # zlib/deflate compression support. This DOWNLOADS zlib 1.3.1 on the first configure --
    # the first superbuild on a machine needs internet access (or a pre-populated build tree).
    -DHDF5_ENABLE_ZLIB_SUPPORT=ON
    -DHDF5_ALLOW_EXTERNAL_SUPPORT=TGZ
    -DZLIB_TGZ_ORIGPATH=https://github.com/madler/zlib/releases/download/v1.3.1
    -DZLIB_TGZ_NAME=zlib-1.3.1.tar.gz
    -DHDF5_USE_ZLIB_STATIC=ON
    -DCMAKE_INSTALL_PREFIX=${HDF5_INST}
  )

  # /D__STDC_NO_COMPLEX__ is MSVC syntax. Passing it unconditionally made every non-Windows HDF5
  # build fail on an unrecognised compiler flag; clang/gcc want -D.
  if(MSVC)
    list(APPEND _hdf5_cmake_args -DCMAKE_C_FLAGS=/D__STDC_NO_COMPLEX__)
  else()
    list(APPEND _hdf5_cmake_args -DCMAKE_C_FLAGS=-D__STDC_NO_COMPLEX__)
  endif()

  ExternalProject_Add(hdf5_ep
    SOURCE_DIR  "${HDF5_SRC}"
    BINARY_DIR  "${HDF5_BUILD}"
    INSTALL_DIR "${HDF5_INST}"
    CMAKE_ARGS  ${_hdf5_cmake_args}
    BUILD_COMMAND   ${CMAKE_COMMAND} --build . --config Release --parallel
    INSTALL_COMMAND ${CMAKE_COMMAND} --build . --target install --config Release --parallel
    UPDATE_DISCONNECTED 1
    USES_TERMINAL_BUILD 1
  )
endif()

# =============================================================================================
# 3) MobiusIfcBridge -- the IFC++ shim DLL consumed by MobiusIfcLibrary (Win64/MSVC only)
# =============================================================================================
#
# This delegates to Source/ThirdParty/IfcBridgeSource/Build-MobiusIfcBridge.ps1 instead of
# re-expressing the recipe as two more ExternalProject_Add calls. That is deliberate. The script
# already encodes several things that are easy to get wrong and expensive to discover twice:
#
#   * IfcPlusPlus must be configured with BUILD_VIEWER_APPLICATION=OFF and
#     BUILD_CONSOLE_APPLICATION=OFF, or configure dies looking for Qt5 + OpenSceneGraph and the
#     failure reads like an IFC++ problem when it is not.
#   * IfcPlusPlus declares cmake_minimum_required(3.6), which modern CMake rejects outright, so a
#     CMAKE_POLICY_VERSION_MINIMUM override is mandatory.
#   * The shim's standalone mode needs MOBIUS_IFCPLUSPLUS_LIB_DIR = <build>/IfcPlusPlus/<Config>,
#     a multi-config generator path that is not known at superbuild configure time.
#   * It verifies all three install outputs afterwards and fails loudly if any is missing.
#
# Duplicating that here would give the project two sources of truth for one build, and the drift
# between them resurfaces as exactly the UBT "run the CMake install step" error this section
# exists to prevent. One recipe, two entry points.
#
# Modelled as add_custom_command(OUTPUT ...) rather than a bare target so an already-built tree
# no-ops: the IfcPlusPlus pass is a ~310 MB build tree with a 155 MB static lib and takes a long
# time, and it must not re-run on every incremental superbuild.
set(IFC_BRIDGE_DLL "${IFC_INSTALL_DIR}/bin/MobiusIfcBridge.dll")
set(IFC_BRIDGE_LIB "${IFC_INSTALL_DIR}/lib/MobiusIfcBridge.lib")

set(_ifc_supported FALSE)
if(SUPERBUILD_BUILD_IFC)
  if(NOT WIN32)
    message(STATUS "MobiusIfcBridge: skipped -- Win64/MSVC only today (MobiusIfcLibrary.Build.cs hard-fails on other platforms).")
  elseif(NOT EXISTS "${IFC_BRIDGE_SRC}/CMakeLists.txt")
    message(WARNING
      "MobiusIfcBridge: skipped -- vendored IFC++ bridge source not found at ${IFC_BRIDGE_SRC}. "
      "The Unreal build will fail on MobiusIfcLibrary until this is present and built.")
  else()
    find_program(MOBIUS_POWERSHELL_EXE
      NAMES pwsh powershell
      DOC "PowerShell used to drive Build-MobiusIfcBridge.ps1 (PowerShell 7 'pwsh' or Windows PowerShell 5.1)")
    if(NOT MOBIUS_POWERSHELL_EXE)
      message(WARNING
        "MobiusIfcBridge: skipped -- neither 'pwsh' nor 'powershell' was found on PATH. "
        "Build it by hand: powershell -File \"${IFC_BRIDGE_SRC}/Build-MobiusIfcBridge.ps1\"")
    else()
      set(_ifc_supported TRUE)
    endif()
  endif()
endif()

if(_ifc_supported)
  # Forward the resolved toolset so IFC++, Assimp and HDF5 are all built with the same MSVC ABI.
  # CMAKE_GENERATOR_TOOLSET arrives as e.g. "version=14.38.33130"; the script wants the bare version.
  #
  # The sentinel matters: `powershell -File script.ps1 -Toolset ""` silently DROPS the empty
  # argument, and the script then dies with "Missing an argument for parameter 'Toolset'". Pass the
  # word "none" instead of an empty string to mean "use the generator default".
  set(_ifc_toolset_arg "none")
  if(CMAKE_GENERATOR_TOOLSET MATCHES "version=([0-9.]+)")
    set(_ifc_toolset_arg "${CMAKE_MATCH_1}")
  elseif(CMAKE_GENERATOR_TOOLSET MATCHES "^[0-9.]+$")
    set(_ifc_toolset_arg "${CMAKE_GENERATOR_TOOLSET}")
  endif()

  set(_ifc_command
    "${MOBIUS_POWERSHELL_EXE}" -NoProfile -ExecutionPolicy Bypass
    -File "${IFC_BRIDGE_SRC}/Build-MobiusIfcBridge.ps1"
    -Config "${SUPERBUILD_IFC_CONFIG}"
    -Generator "${CMAKE_GENERATOR}"
    -Toolset "${_ifc_toolset_arg}"
    -PolicyMinimum "${SUPERBUILD_IFC_POLICY_MIN}"
  )

  if(SUPERBUILD_IFC_FORCE)
    # A plain target always runs; use this when you have edited MobiusIfcBridge.cpp/.h and want the
    # shim rebuilt unconditionally. The IfcPlusPlus pass still short-circuits inside the script
    # because its own CMake build tree is incremental.
    add_custom_target(ifc_bridge ALL
      COMMAND ${_ifc_command}
      COMMENT "Building MobiusIfcBridge (forced) -> ${IFC_INSTALL_DIR}"
      USES_TERMINAL
      VERBATIM
    )
  else()
    add_custom_command(
      OUTPUT "${IFC_BRIDGE_DLL}" "${IFC_BRIDGE_LIB}"
      COMMAND ${_ifc_command}
      DEPENDS
        "${IFC_BRIDGE_SRC}/MobiusIfcBridge.cpp"
        "${IFC_BRIDGE_SRC}/MobiusIfcBridge.h"
        "${IFC_BRIDGE_SRC}/CMakeLists.txt"
        "${IFC_BRIDGE_SRC}/Build-MobiusIfcBridge.ps1"
      COMMENT "Building IFC++ and MobiusIfcBridge -> ${IFC_INSTALL_DIR} (first run takes a while)"
      USES_TERMINAL
      VERBATIM
    )
    add_custom_target(ifc_bridge ALL DEPENDS "${IFC_BRIDGE_DLL}" "${IFC_BRIDGE_LIB}")
  endif()
endif()

# ---------------------------------------------------------------------------------------------
# Convenience umbrella target
# ---------------------------------------------------------------------------------------------
add_custom_target(all_external
  COMMENT "All external dependencies built and staged"
)
add_dependencies(all_external sync_builddocs)
if(SUPERBUILD_BUILD_ASSIMP)
  add_dependencies(all_external assimp_stage)
endif()
if(SUPERBUILD_BUILD_HDF5)
  add_dependencies(all_external hdf5_ep)
endif()
if(_ifc_supported)
  add_dependencies(all_external ifc_bridge)
endif()

# =============================================================================================
# Staging checks (ctest)
# =============================================================================================
#
# These answer one question: "is every artifact UnrealBuildTool will look for actually on disk?"
# Each check names the file it wanted, so a failure reads as the same sentence UBT would print.
if(SUPERBUILD_ENABLE_TESTS)
  function(add_path_exists_test Name PathToCheck)
    set(_script "${CMAKE_BINARY_DIR}/Testing/${Name}-check.cmake")
    file(WRITE "${_script}"
      "if(NOT EXISTS \"${PathToCheck}\")\n"
      "  message(FATAL_ERROR \"Missing staged artifact: ${PathToCheck}\")\n"
      "else()\n"
      "  message(STATUS \"Found staged artifact: ${PathToCheck}\")\n"
      "endif()\n")
    add_test(NAME ${Name} COMMAND ${CMAKE_COMMAND} -P "${_script}")
  endfunction()

  # Directory-exists is not enough where UBT globs for a file. UE_AssimpLibrary.Build.cs does
  #   Directory.GetFiles(LibDir, "assimp*.lib")[0]
  # so an EMPTY but present lib/Win64 -- which is exactly what a failed install leaves behind after
  # the prune step has already run -- passes a directory check and then throws
  # IndexOutOfRangeException from inside UBT, with no indication of which file was missing. Glob
  # for the real thing instead. Globbing happens at TEST time, not configure time, because these
  # files do not exist yet when this file is processed.
  function(add_glob_matches_test Name Directory Pattern)
    set(_script "${CMAKE_BINARY_DIR}/Testing/${Name}-check.cmake")
    file(WRITE "${_script}"
      "file(GLOB _matches \"${Directory}/${Pattern}\")\n"
      "if(_matches STREQUAL \"\")\n"
      "  message(FATAL_ERROR \"No file matching '${Pattern}' in ${Directory}. \"\n"
      "    \"UnrealBuildTool globs this directory and will fail with an index error rather than a \"\n"
      "    \"readable message. Re-run the superbuild.\")\n"
      "else()\n"
      "  message(STATUS \"Found: \${_matches}\")\n"
      "endif()\n")
    add_test(NAME ${Name} COMMAND ${CMAKE_COMMAND} -P "${_script}")
  endfunction()

  add_path_exists_test(builddocs_build_and_run   "${BUILD_DOCS_MIRROR_DIR}/BUILD-AND-RUN.md")
  add_path_exists_test(builddocs_agent_sample_data "${BUILD_DOCS_MIRROR_DIR}/AGENT-SAMPLE-DATA.md")
  add_path_exists_test(builddocs_research        "${BUILD_DOCS_MIRROR_DIR}/RESEARCH.md")

  if(SUPERBUILD_BUILD_HDF5)
    add_path_exists_test(hdf5_include_dir "${HDF5_INST}/include")
    add_path_exists_test(hdf5_lib_dir     "${HDF5_INST}/lib")
    if(WIN32)
      add_path_exists_test(hdf5_static_lib_core "${HDF5_INST}/lib/libhdf5.lib")
      add_path_exists_test(hdf5_static_lib_hl   "${HDF5_INST}/lib/libhdf5_hl.lib")
      add_path_exists_test(hdf5_static_lib_zlib "${HDF5_INST}/lib/zlib-static.lib")
    elseif(APPLE)
      add_path_exists_test(hdf5_static_lib_core "${HDF5_INST}/lib/libhdf5.a")
      add_path_exists_test(hdf5_static_lib_hl   "${HDF5_INST}/lib/libhdf5_hl.a")
      add_path_exists_test(hdf5_static_lib_zlib "${HDF5_INST}/lib/libzlib-static.a")
    endif()
  endif()

  # Assimp: the include dir is what UE_AssimpLibrary.Build.cs probes first, and the staged runtime
  # is what the editor actually loads. Neither was checked before.
  if(SUPERBUILD_BUILD_ASSIMP)
    add_path_exists_test(assimp_include_dir "${ASSIMP_INST}/include/assimp/version.h")
    if(WIN32)
      add_glob_matches_test(assimp_import_lib "${ASSIMP_INST}/lib/Win64" "assimp*.lib")
      add_glob_matches_test(assimp_runtime_dll "${ASSIMP_INST}/bin/Win64" "assimp*.dll")
      add_glob_matches_test(assimp_staged_dll "${ASSIMP_STAGE_DIR}" "assimp*.dll")
    endif()
  endif()

  # IFC: these three are precisely the paths MobiusIfcLibrary.Build.cs throws a BuildException on.
  if(_ifc_supported)
    add_path_exists_test(ifc_bridge_header "${IFC_INSTALL_DIR}/include/MobiusIfcBridge.h")
    add_path_exists_test(ifc_bridge_lib    "${IFC_BRIDGE_LIB}")
    add_path_exists_test(ifc_bridge_dll    "${IFC_BRIDGE_DLL}")
  endif()

  # Optional Unreal automation and GenerateProjectFiles/build checks
  if(SUPERBUILD_ENABLE_UE_AUTOMATION AND NOT UE_AUTOMATION_EXE STREQUAL "")
    if(EXISTS "${UE_AUTOMATION_EXE}")
      separate_arguments(_UE_AUTOMATION_ARGS NATIVE_COMMAND "${UE_AUTOMATION_ARGS}")
      add_test(NAME ue_automation COMMAND "${UE_AUTOMATION_EXE}" ${_UE_AUTOMATION_ARGS})
      set_tests_properties(ue_automation PROPERTIES TIMEOUT 900)
    else()
      message(STATUS "UE automation exe not found at '${UE_AUTOMATION_EXE}'. Skipping ue_automation test.")
    endif()
  endif()

  set(_UE_GPF_TEST_EXISTS FALSE)
  if(SUPERBUILD_ENABLE_UE_GENBUILD AND NOT UE_GENERATE_FILES_EXE STREQUAL "")
    if(EXISTS "${UE_GENERATE_FILES_EXE}")
      separate_arguments(_UE_GPF_ARGS NATIVE_COMMAND "${UE_GENERATE_FILES_ARGS}")
      add_test(NAME ue_generate_project_files
        COMMAND "${UE_GENERATE_FILES_EXE}" ${_UE_GPF_ARGS}
        WORKING_DIRECTORY "${UE_ROOT}")
      set_tests_properties(ue_generate_project_files PROPERTIES TIMEOUT 900)
      set(_UE_GPF_TEST_EXISTS TRUE)
    else()
      message(STATUS "GenerateProjectFiles not found at '${UE_GENERATE_FILES_EXE}'. Skipping ue_generate_project_files test.")
    endif()
  endif()

  if(SUPERBUILD_ENABLE_UE_GENBUILD AND NOT UE_BUILD_EXE STREQUAL "")
    if(EXISTS "${UE_BUILD_EXE}")
      separate_arguments(_UE_BUILD_ARGS NATIVE_COMMAND "${UE_BUILD_ARGS}")
      add_test(NAME ue_build_editor
        COMMAND "${UE_BUILD_EXE}" ${_UE_BUILD_ARGS}
        WORKING_DIRECTORY "${UE_ROOT}")
      set_tests_properties(ue_build_editor PROPERTIES TIMEOUT 1800)
      if(_UE_GPF_TEST_EXISTS)
        set_tests_properties(ue_build_editor PROPERTIES DEPENDS ue_generate_project_files)
      endif()
    else()
      message(STATUS "UE build helper not found at '${UE_BUILD_EXE}'. Skipping ue_build_editor test.")
    endif()
  endif()

  if(SUPERBUILD_RUN_TESTS)
    # `superbuild_tests` previously lacked ALL, so SUPERBUILD_RUN_TESTS=ON silently did nothing on a
    # plain `cmake --build`. It is part of ALL now, and it runs last because it depends on
    # all_external.
    add_custom_target(superbuild_tests ALL
      COMMENT "Running superbuild staging checks"
    )
    add_dependencies(superbuild_tests all_external)

    # Multi-config generators (e.g. MSVC) need an explicit configuration for ctest.
    if(CMAKE_CONFIGURATION_TYPES)
      set(_CTEST_CONFIG_ARGS "-C" "${SUPERBUILD_CTEST_CONFIG}")
    else()
      set(_CTEST_CONFIG_ARGS)
    endif()

    set(_CTEST_WRAPPER "${CMAKE_BINARY_DIR}/Testing/run_ctest.cmake")
    if(SUPERBUILD_TESTS_ARE_FATAL)
      set(_CTEST_FAILURE_ACTION "FATAL_ERROR")
    else()
      set(_CTEST_FAILURE_ACTION "WARNING")
    endif()
    # Two renderings of the same thing: a quoted one for the COMMAND line, and a bare one for the
    # human-readable message (quotes nested inside a message() string produce a CMake syntax
    # warning and print as literal quote characters in the copy-pasteable command).
    string(REPLACE ";" "\" \"" _CTEST_CONFIG_ARGS_TEXT "${_CTEST_CONFIG_ARGS}")
    if(_CTEST_CONFIG_ARGS_TEXT)
      set(_CTEST_CONFIG_ARGS_TEXT "\"${_CTEST_CONFIG_ARGS_TEXT}\"")
    endif()
    string(REPLACE ";" " " _CTEST_CONFIG_ARGS_PLAIN "${_CTEST_CONFIG_ARGS}")

    file(WRITE "${_CTEST_WRAPPER}" "
execute_process(
  COMMAND \"${CMAKE_CTEST_COMMAND}\" --output-on-failure --output-log \"${CMAKE_BINARY_DIR}/Testing/superbuild-ctest.log\" ${_CTEST_CONFIG_ARGS_TEXT}
  WORKING_DIRECTORY \"${CMAKE_BINARY_DIR}\"
  RESULT_VARIABLE _ctest_result
)
if(NOT _ctest_result EQUAL 0)
  message(${_CTEST_FAILURE_ACTION}
    \"Superbuild staging checks FAILED (ctest exit code \${_ctest_result}).\\n\"
    \"Each failure names the exact file UnrealBuildTool will also fail to find.\\n\"
    \"Full log: ${CMAKE_BINARY_DIR}/Testing/superbuild-ctest.log\\n\"
    \"Re-run just the checks with:  ctest --test-dir ${CMAKE_BINARY_DIR} ${_CTEST_CONFIG_ARGS_PLAIN} --output-on-failure\")
else()
  message(STATUS \"Superbuild staging checks passed -- every artifact UnrealBuildTool needs is on disk.\")
endif()
")

    add_custom_command(TARGET superbuild_tests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/Testing"
      COMMAND ${CMAKE_COMMAND} -P "${_CTEST_WRAPPER}"
      COMMENT "Executing ctest for staged dependencies (log: ${CMAKE_BINARY_DIR}/Testing/superbuild-ctest.log)"
    )
  endif()
endif()

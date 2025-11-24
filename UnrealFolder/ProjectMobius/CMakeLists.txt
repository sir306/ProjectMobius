cmake_minimum_required(VERSION 3.21)
project(MobiusSuperbuild LANGUAGES C CXX)

# ======================
# Global build settings
# ======================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to a user-writable install prefix if none is provided
if(NOT DEFINED CMAKE_INSTALL_PREFIX OR CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  if(WIN32)
    set(_DEFAULT_PREFIX "$ENV{USERPROFILE}/MobiusSuperbuild")
  else()
    set(_DEFAULT_PREFIX "$ENV{HOME}/MobiusSuperbuild")
  endif()
  set(CMAKE_INSTALL_PREFIX "${_DEFAULT_PREFIX}" CACHE PATH "Install prefix" FORCE)
endif()

message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")

# Usage (Windows VS generator):
#   cmake -S . -B _superbuild -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.9.2/msvc2022_64"
#   cmake --build _superbuild --config Release --parallel
#
# Usage (macOS Ninja):
#   cmake -S . -B _superbuild -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$HOME/Qt/6.9.2/macos"
#   cmake --build _superbuild --parallel

include(ExternalProject)

# --- Project roots (adjust if your tree differs) ---
set(UE_ROOT "${CMAKE_SOURCE_DIR}")
set(TOOLS_ROOT "${UE_ROOT}/Tools/QT_Apps")

set(OPENFILETCP_SRC "${TOOLS_ROOT}/OpenFileTCP")     # has its own deploy target
set(PLOTUE_SRC      "${TOOLS_ROOT}/PlotUE_Data")     # installs via `install()`
set(ASSIMP_SRC      "${UE_ROOT}/Plugins/UE4_Assimp/Source/ThirdParty/UE_AssimpLibrary/assimp")

# --- Platform output folders Unreal expects ---
if(WIN32)
  set(PLAT Win64)
elseif(APPLE)
  set(PLAT Mac)
else()
  set(PLAT Linux)
endif()

# Plugin runtime dir where UBT/packager picks up DLLs/DYLIBs
set(ASSIMP_STAGE_DIR "${UE_ROOT}/Plugins/UE4_Assimp/Binaries/${PLAT}")

# Tools staging (so UE can launch them with CreateProc)
set(TOOLS_STAGE_DIR  "${UE_ROOT}/Tools/bin/${PLAT}")

# ---------------------------------------
# 1) Build Assimp as shared and INSTALL into the plugin's ThirdParty layout
# ---------------------------------------
set(ASSIMP_BUILD "${CMAKE_BINARY_DIR}/assimp-build")

# Install directly where UE modules expect it:
set(ASSIMP_INST "${UE_ROOT}/Plugins/UE4_Assimp/Source/ThirdParty/UE_AssimpLibrary/assimp")

# Common args
set(_assimp_cmake_args
  -DCMAKE_BUILD_TYPE=Release
  -DBUILD_SHARED_LIBS=ON
  -DASSIMP_BUILD_TESTS=OFF
  -DASSIMP_BUILD_ASSIMP_TOOLS=OFF
  -DASSIMP_BUILD_SAMPLES=OFF
  -DCMAKE_INSTALL_PREFIX=${ASSIMP_INST}
)

# Per-platform install subdirs that match UE_Assimp(.Build.cs)
if(WIN32)
  list(APPEND _assimp_cmake_args
    -DASSIMP_BUILD_ZLIB=ON
    -DASSIMP_BIN_INSTALL_DIR=bin/Win64      
    -DASSIMP_LIB_INSTALL_DIR=lib/Win64      
    -DASSIMP_INCLUDE_INSTALL_DIR=include   
  )
elseif(APPLE)
  # Build Assimp as arm64 (Apple Silicon) and install libs under lib/Mac so the
  # UE_Assimp plugin's Build.cs and SDK checks can find them without hacks.
  list(APPEND _assimp_cmake_args
    -DASSIMP_BUILD_ZLIB=OFF
    -DCMAKE_MACOSX_RPATH=ON
    -DCMAKE_INSTALL_NAME_DIR=@rpath
    -DCMAKE_OSX_ARCHITECTURES=x86_64\;arm64
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0

    # Assimp on UNIX derives ASSIMP_LIB_INSTALL_DIR from CMAKE_INSTALL_LIBDIR.
    # Force libs into lib/Mac to match UE_Assimp expectations.
    -DCMAKE_INSTALL_LIBDIR=lib/Mac

    -DASSIMP_INCLUDE_INSTALL_DIR=include
  )
else()
  list(APPEND _assimp_cmake_args
    -DASSIMP_BUILD_ZLIB=OFF
    -DASSIMP_BIN_INSTALL_DIR=bin/Linux
    -DASSIMP_LIB_INSTALL_DIR=lib/Linux
    -DASSIMP_INCLUDE_INSTALL_DIR=include
  )
endif()

ExternalProject_Add(assimp_ep
  SOURCE_DIR  "${ASSIMP_SRC}"
  BINARY_DIR  "${ASSIMP_BUILD}"
  INSTALL_DIR "${ASSIMP_INST}"
  CMAKE_ARGS  ${_assimp_cmake_args}
  BUILD_COMMAND   ${CMAKE_COMMAND} --build . --config Release --parallel
  INSTALL_COMMAND ${CMAKE_COMMAND} --build . --target install --config Release --parallel
  UPDATE_DISCONNECTED 1
)

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

# Stage Assimp runtime into the plugin’s Binaries/<Platform> for editor runs
add_custom_target(assimp_stage ALL
  DEPENDS assimp_ep
  COMMAND ${CMAKE_COMMAND} -E make_directory "${ASSIMP_STAGE_DIR}"
)

if(WIN32)
  add_custom_command(TARGET assimp_stage POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${ASSIMP_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ASSIMP_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ASSIMP_INST}/bin/Win64" "${ASSIMP_STAGE_DIR}"
    COMMENT "Staging Assimp DLLs to ${ASSIMP_STAGE_DIR}"
  )
elseif(APPLE)
  # Assimp 5.x layouts differ a bit between versions.
  # Prefer lib/Mac if it exists (old layout), otherwise fall back to lib/.
  if(EXISTS "${ASSIMP_INST}/lib/Mac")
    set(ASSIMP_MAC_LIB_DIR "${ASSIMP_INST}/lib/Mac")
  else()
    set(ASSIMP_MAC_LIB_DIR "${ASSIMP_INST}/lib")
  endif()

  add_custom_command(TARGET assimp_stage POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${ASSIMP_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ASSIMP_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ASSIMP_MAC_LIB_DIR}" "${ASSIMP_STAGE_DIR}"
    COMMENT "Staging Assimp dylibs to ${ASSIMP_STAGE_DIR}"
  )
else()
  add_custom_command(TARGET assimp_stage POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${ASSIMP_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ASSIMP_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ASSIMP_INST}/lib/Linux" "${ASSIMP_STAGE_DIR}"
    COMMENT "Staging Assimp .so to ${ASSIMP_STAGE_DIR}"
  )
endif()

# ------------------------------------------------------------
# 2) Build OpenFileTCP and run its deploy step (Win/Mac)
#    OpenFileTCP CMake already defaults Qt paths & defines DeployOpenFileTCP.
# ------------------------------------------------------------
set(OPENFILETCP_BUILD "${CMAKE_BINARY_DIR}/OpenFileTCP-build")

ExternalProject_Add(OpenFileTCP_ep
  SOURCE_DIR  "${OPENFILETCP_SRC}"
  BINARY_DIR  "${OPENFILETCP_BUILD}"
  CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=Release
    # inherit a user-provided Qt path if given at superbuild configure time
    -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
  BUILD_COMMAND   ${CMAKE_COMMAND} --build . --config Release --target OpenFileTCP --parallel
  INSTALL_COMMAND ""   # deploy handled as a separate step
  UPDATE_DISCONNECTED 1
  DEPENDS assimp_stage
)

# Call the app's deploy target as a dedicated step (portable; no '&&')
ExternalProject_Add_Step(OpenFileTCP_ep deploy
  COMMAND ${CMAKE_COMMAND} --build . --config Release --target DeployOpenFileTCP --parallel
  DEPENDEES build
  WORKING_DIRECTORY "${OPENFILETCP_BUILD}"
)

# Stage deployed output to Tools/bin/<Platform>
add_custom_target(OpenFileTCP_stage ALL
  DEPENDS OpenFileTCP_ep
  COMMAND ${CMAKE_COMMAND} -E make_directory "${TOOLS_STAGE_DIR}"
)

if(WIN32)
  add_custom_command(TARGET OpenFileTCP_stage POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${OPENFILETCP_BUILD}/WindowsExe" "${TOOLS_STAGE_DIR}/OpenFileTCP"
  )
elseif(APPLE)
  add_custom_command(TARGET OpenFileTCP_stage POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${OPENFILETCP_BUILD}/MacExe/OpenFileTCP.app" "${TOOLS_STAGE_DIR}/OpenFileTCP.app"
  )
endif()

# ------------------------------------------------------------
# 3) Build PlotUE_Data, install it, then stage for UE
#    (Forward parent CMAKE_PREFIX_PATH so 'find_package(Qt6 ... Graphs)' works.)
# ------------------------------------------------------------
set(PLOTUE_BUILD "${CMAKE_BINARY_DIR}/PlotUE_Data-build")

ExternalProject_Add(PlotUE_Data_ep
  SOURCE_DIR  "${PLOTUE_SRC}"
  BINARY_DIR  "${PLOTUE_BUILD}"
  CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}   # forward Qt path
  BUILD_COMMAND   ${CMAKE_COMMAND} --build . --config Release --target appPlotUE_Data --parallel
  INSTALL_COMMAND ""   # deployment handled via custom target
  UPDATE_DISCONNECTED 1
)

# invoke its deploy target (created in step 1)
ExternalProject_Add_Step(PlotUE_Data_ep deploy
  COMMAND ${CMAKE_COMMAND} --build . --config Release --target DeployPlotUE_Data --parallel
  DEPENDEES build
  WORKING_DIRECTORY "${PLOTUE_BUILD}"
)

add_custom_target(PlotUE_Data_stage ALL
  DEPENDS PlotUE_Data_ep
  COMMAND ${CMAKE_COMMAND} -E make_directory "${TOOLS_STAGE_DIR}"
)

if(WIN32)
  add_custom_command(TARGET PlotUE_Data_stage POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PLOTUE_BUILD}/WindowsExe" "${TOOLS_STAGE_DIR}/PlotUE_Data"
    COMMENT "Staging PlotUE_Data to ${TOOLS_STAGE_DIR}/PlotUE_Data"
  )
elseif(APPLE)
  add_custom_command(TARGET PlotUE_Data_stage POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PLOTUE_BUILD}/appPlotUE_Data.app"
            "${TOOLS_STAGE_DIR}/PlotUE_Data.app"
    COMMENT "Staging PlotUE_Data.app from ${PLOTUE_BUILD}"
  )
endif()

# Convenience umbrella target
add_custom_target(all_external
  DEPENDS assimp_stage OpenFileTCP_stage PlotUE_Data_stage
)

add_dependencies(all_external
  assimp_ep
  PlotUE_Data_ep
  OpenFileTCP_ep
)

# ======================
# Convenience messages
# ======================
message(STATUS "Superbuild will stage outputs in: ${CMAKE_BINARY_DIR}")
message(STATUS "Node bundle output: ${CMAKE_BINARY_DIR}/nodejs-build/bundle")
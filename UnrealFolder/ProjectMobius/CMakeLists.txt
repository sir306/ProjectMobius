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
# 1) Build Assimp as shared and stage it
# ---------------------------------------
set(ASSIMP_BUILD "${CMAKE_BINARY_DIR}/assimp-build")
set(ASSIMP_INST  "${CMAKE_BINARY_DIR}/assimp-install")

# Build arg list without generator expressions to avoid dev warnings
set(_assimp_cmake_args
  -DCMAKE_BUILD_TYPE=Release
  -DBUILD_SHARED_LIBS=ON
  -DASSIMP_BUILD_TESTS=OFF
  -DASSIMP_BUILD_ASSIMP_TOOLS=OFF
  -DASSIMP_BUILD_SAMPLES=OFF
  -DCMAKE_INSTALL_PREFIX=${ASSIMP_INST}
)

if(WIN32)
  list(APPEND _assimp_cmake_args -DASSIMP_BUILD_ZLIB=ON)
elseif(APPLE)
  list(APPEND _assimp_cmake_args
    -DASSIMP_BUILD_ZLIB=OFF
    -DCMAKE_MACOSX_RPATH=ON
    -DCMAKE_INSTALL_NAME_DIR=@rpath
    -DCMAKE_OSX_ARCHITECTURES=x86_64\;arm64
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
  )
else()
  list(APPEND _assimp_cmake_args -DASSIMP_BUILD_ZLIB=OFF)
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

# Stage Assimp runtime into the plugin’s Binaries/<Platform>
add_custom_target(assimp_stage ALL
  DEPENDS assimp_ep
  COMMAND ${CMAKE_COMMAND} -E make_directory "${ASSIMP_STAGE_DIR}"
)

if(WIN32)
  add_custom_command(TARGET assimp_stage POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ASSIMP_INST}/bin" "${ASSIMP_STAGE_DIR}"
    COMMENT "Staging Assimp DLLs to ${ASSIMP_STAGE_DIR}"
  )
elseif(APPLE)
  add_custom_command(TARGET assimp_stage POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ASSIMP_INST}/lib" "${ASSIMP_STAGE_DIR}"
    COMMENT "Staging Assimp dylibs to ${ASSIMP_STAGE_DIR}"
  )
endif()

# ------------------------------------------------------------
# 2) Build OpenFileTCP and run its deploy step (Win/Mac)
#    OpenFileTCP CMake already defaults Qt paths & defines DeployOpenFileTCP. (see its CMake)  [ref]
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
            "${PLOTUE_BUILD}/MacExe/appPlotUE_Data.app"
            "${TOOLS_STAGE_DIR}/PlotUE_Data.app"
    COMMENT "Staging PlotUE_Data.app from ${PLOTUE_BUILD}/MacExe"
  )
endif()

# Convenience umbrella target
add_custom_target(all_external
  DEPENDS assimp_stage OpenFileTCP_stage PlotUE_Data_stage
)
# ------------------------------------------------------------
# 4) Package NodeJS WebSocket server and stage alongside tools
#     (Tools/bin/<Platform>/NodeJS/mobius-server[.exe])
# ------------------------------------------------------------
# ======================
# NodeJS server (Tools/NodeJS)
# ======================
# Ensure the NodeJS subproject is added but does NOT define global targets
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/Tools/NodeJS
                 ${CMAKE_BINARY_DIR}/nodejs-build)

# Ensure our umbrella target exists
if(NOT TARGET all_external)
  add_custom_target(all_external)
endif()

# Wait for the node bundle to be produced
add_dependencies(all_external mobius_node_bundle)

# --- Stage/rename for UE runtime lookup (WebSocketSubsystem) ---
set(NODEJS_STAGE_DIR "${TOOLS_STAGE_DIR}/NodeJS")
file(MAKE_DIRECTORY "${NODEJS_STAGE_DIR}")

if(WIN32)
  set(NODE_SRC_EXE "${CMAKE_BINARY_DIR}/bundle/MobiusServer.exe")
  set(NODE_DST_EXE "${NODEJS_STAGE_DIR}/mobius-server.exe")  # EXACT name UE expects
  add_custom_target(nodejs_stage ALL
    DEPENDS mobius_node_bundle
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NODE_SRC_EXE}" "${NODE_DST_EXE}"
    COMMENT "Staging NodeJS server to ${NODE_DST_EXE}"
  )
elseif(APPLE)
  # Build produces a single "MobiusServer" universal for chosen arch
  set(NODE_SRC_EXE "${CMAKE_BINARY_DIR}/bundle/MobiusServer")
  # Provide both names if you want to be explicit or keep one — your UE code prefers arm then x64. :contentReference[oaicite:6]{index=6}
  add_custom_target(nodejs_stage ALL
    DEPENDS mobius_node_bundle
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NODE_SRC_EXE}" "${NODEJS_STAGE_DIR}/mobius-server-macos-arm64"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NODE_SRC_EXE}" "${NODEJS_STAGE_DIR}/mobius-server-macos-x64"
    COMMENT "Staging NodeJS server to ${NODEJS_STAGE_DIR}"
  )
endif()

add_dependencies(all_external
  assimp_ep
  PlotUE_Data_ep
  OpenFileTCP_ep
  nodejs_stage
)

# ======================
# Convenience messages
# ======================
message(STATUS "Superbuild will stage outputs in: ${CMAKE_BINARY_DIR}")
message(STATUS "Node bundle output: ${CMAKE_BINARY_DIR}/nodejs-build/bundle")
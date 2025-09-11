cmake_minimum_required(VERSION 3.21)
project(MobiusSuperbuild LANGUAGES C CXX)

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
set(PLOTUE_INST  "${CMAKE_BINARY_DIR}/PlotUE_Data-install")

ExternalProject_Add(PlotUE_Data_ep
  SOURCE_DIR  "${PLOTUE_SRC}"
  BINARY_DIR  "${PLOTUE_BUILD}"
  INSTALL_DIR "${PLOTUE_INST}"
  CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX=${PLOTUE_INST}
    -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
  BUILD_COMMAND   ${CMAKE_COMMAND} --build . --config Release --parallel
  INSTALL_COMMAND ${CMAKE_COMMAND} --build . --target install --config Release --parallel
  UPDATE_DISCONNECTED 1
)

add_custom_target(PlotUE_Data_stage ALL
  DEPENDS PlotUE_Data_ep
  COMMAND ${CMAKE_COMMAND} -E make_directory "${TOOLS_STAGE_DIR}"
)

if(WIN32)
  add_custom_command(TARGET PlotUE_Data_stage POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PLOTUE_INST}/bin" "${TOOLS_STAGE_DIR}/PlotUE_Data"
  )
elseif(APPLE)
  add_custom_command(TARGET PlotUE_Data_stage POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PLOTUE_INST}/appPlotUE_Data.app" "${TOOLS_STAGE_DIR}/PlotUE_Data.app"
  )
endif()

# Convenience umbrella target
add_custom_target(all_external
  DEPENDS assimp_stage OpenFileTCP_stage PlotUE_Data_stage
)

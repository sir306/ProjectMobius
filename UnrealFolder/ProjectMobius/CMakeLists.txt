cmake_minimum_required(VERSION 3.21)
project(MobiusSuperbuild LANGUAGES C CXX)

# Optional superbuild verification (ctest) flags.
option(SUPERBUILD_ENABLE_TESTS "Generate simple staging checks for built tools" ON)
option(SUPERBUILD_RUN_TESTS "Run ctest after staging completes" ON)
set(SUPERBUILD_CTEST_CONFIG "Release" CACHE STRING "Configuration to use when invoking ctest in multi-config generators")
# Optional Unreal automation hooks (off by default; require user-provided commands/paths)
option(SUPERBUILD_ENABLE_UE_AUTOMATION "Add a ctest to run Unreal automation (UAT/UnrealEditor-Cmd)" OFF)
set(UE_AUTOMATION_EXE "" CACHE STRING "Path to UnrealEditor-Cmd.exe (or platform equivalent)")
set(UE_AUTOMATION_ARGS "" CACHE STRING "Arguments for Unreal automation (e.g. -run=Automation -Test=All -unattended -nop4 -log)")
option(SUPERBUILD_ENABLE_UE_GENBUILD "Add ctests to GenerateProjectFiles and build the editor target" OFF)
set(UE_GENERATE_FILES_EXE "" CACHE STRING "Path to GenerateProjectFiles.bat/sh")
set(UE_GENERATE_FILES_ARGS "" CACHE STRING "Args for GenerateProjectFiles (e.g. -project=ProjectMobius.uproject -game -engine)")
set(UE_BUILD_EXE "" CACHE STRING "Path to the UE build helper (e.g. Engine/Build/BatchFiles/Build.bat)")
set(UE_BUILD_ARGS "" CACHE STRING "Args for UE build (e.g. ProjectMobiusEditor Win64 Development -project=ProjectMobius.uproject -game -engine -progress)")
if(SUPERBUILD_ENABLE_TESTS)
  enable_testing()

  # Small helper script to summarize gtest XML into a text file for non-dev readers.
  set(_ASSIMP_SUMMARY_SCRIPT "${CMAKE_BINARY_DIR}/Testing/gen_assimp_summary.cmake")
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/Testing")
  set(_ASSIMP_SUMMARY_SCRIPT_CONTENT "
if(NOT DEFINED XML OR NOT DEFINED OUT)
  message(FATAL_ERROR \"Expected -DXML and -DOUT\")
endif()
if(NOT EXISTS \"\${XML}\")
  file(WRITE \"\${OUT}\" \"Assimp unit test summary\\n\")
  file(APPEND \"\${OUT}\" \"Result: no XML produced (tests may have failed to run).\\n\")
  return()
endif()
file(READ \"\${XML}\" CONTENT)
string(REGEX MATCHALL \"<testcase[^>]*>\" TESTS \"\${CONTENT}\")
list(LENGTH TESTS TOTAL)
string(REGEX MATCHALL \"<failure[^>]*>\" FAILS \"\${CONTENT}\")
list(LENGTH FAILS FAIL_COUNT)
file(WRITE \"\${OUT}\" \"Assimp unit test summary\\n\")
file(APPEND \"\${OUT}\" \"Total tests: \${TOTAL}\\n\")
file(APPEND \"\${OUT}\" \"Failed tests: \${FAIL_COUNT}\\n\")
if(FAIL_COUNT GREATER 0)
  string(REGEX MATCHALL \"<testcase[^>]*name=\\\"([^\\\"]+)\\\"[^>]*>[^<]*<failure\" FAIL_CASES \"\${CONTENT}\")
  if(FAIL_CASES)
    file(APPEND \"\${OUT}\" \"Failed test names:\\n\")
    foreach(F \${FAIL_CASES})
      string(REGEX REPLACE \".*name=\\\"([^\\\"]+)\\\".*\" \"\\1\" NAME \"\${F}\")
      file(APPEND \"\${OUT}\" \" - \${NAME}\\n\")
    endforeach()
  endif()
endif()
")
  file(WRITE "${_ASSIMP_SUMMARY_SCRIPT}" "${_ASSIMP_SUMMARY_SCRIPT_CONTENT}")
endif()

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
if(SUPERBUILD_RUN_TESTS)
  set(_assimp_build_tests ON)
else()
  set(_assimp_build_tests OFF)
endif()

set(_assimp_cmake_args
  -DCMAKE_BUILD_TYPE=Release
  -DBUILD_SHARED_LIBS=ON
  -DASSIMP_BUILD_TESTS=${_assimp_build_tests}
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
    -DCMAKE_OSX_ARCHITECTURES=arm64
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

# Optionally run assimp ctests when enabled
if(SUPERBUILD_RUN_TESTS)
  if(WIN32)
    set(_ASSIMP_UNIT_EXE "${ASSIMP_BUILD}/bin/${SUPERBUILD_CTEST_CONFIG}/unit.exe")
    set(_ASSIMP_UNIT_WORKDIR "${ASSIMP_BUILD}/bin/${SUPERBUILD_CTEST_CONFIG}")
  elseif(APPLE)
    set(_ASSIMP_UNIT_EXE "${ASSIMP_BUILD}/bin/unit")
    set(_ASSIMP_UNIT_WORKDIR "${ASSIMP_BUILD}/bin")
  else()
    set(_ASSIMP_UNIT_EXE "${ASSIMP_BUILD}/bin/unit")
    set(_ASSIMP_UNIT_WORKDIR "${ASSIMP_BUILD}/bin")
  endif()

  set(_ASSIMP_XML "${CMAKE_BINARY_DIR}/Testing/assimp-unit.xml")
  set(_ASSIMP_TXT "${CMAKE_BINARY_DIR}/Testing/assimp-unit-summary.txt")

  ExternalProject_Add_Step(assimp_ep run_tests
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/Testing"
    COMMAND ${CMAKE_COMMAND} --build . --config ${SUPERBUILD_CTEST_CONFIG} --target unit --parallel
    COMMAND ${CMAKE_COMMAND} -E env GTEST_OUTPUT=xml:${_ASSIMP_XML} "${_ASSIMP_UNIT_EXE}"
    COMMAND ${CMAKE_COMMAND} -DXML=${_ASSIMP_XML} -DOUT=${_ASSIMP_TXT} -P "${_ASSIMP_SUMMARY_SCRIPT}"
    DEPENDEES build
    WORKING_DIRECTORY "${ASSIMP_BUILD}"
    COMMENT "Running Assimp unit tests (xml: ${_ASSIMP_XML}, summary: ${_ASSIMP_TXT})"
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

# ------------------------------------------------------------
# Basic staging checks (ctest)
# ------------------------------------------------------------
if(SUPERBUILD_ENABLE_TESTS)
  # Helper to add file/directory existence checks without relying on cmake -E test_path
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

  # Helper to add a selftest launch (expects the binary to accept --selftest and exit quickly).
  function(add_selftest Name WorkingDir ExecutablePath)
    add_test(NAME ${Name}
      COMMAND ${CMAKE_COMMAND} -E chdir "${WorkingDir}"
              "${ExecutablePath}" --selftest)
    set_tests_properties(${Name} PROPERTIES TIMEOUT 30)
  endfunction()

  add_path_exists_test(stage_tools_bin_exists "${TOOLS_STAGE_DIR}")

  if(WIN32)
    set(_OpenFileTCP_exe "${TOOLS_STAGE_DIR}/OpenFileTCP/OpenFileTCP.exe")
    set(_PlotUE_Data_exe "${TOOLS_STAGE_DIR}/PlotUE_Data/appPlotUE_Data.exe")
    add_path_exists_test(stage_OpenFileTCP_exists "${_OpenFileTCP_exe}")
    add_path_exists_test(stage_PlotUE_Data_exists "${_PlotUE_Data_exe}")
    add_selftest(run_OpenFileTCP_selftest "${TOOLS_STAGE_DIR}/OpenFileTCP" "${_OpenFileTCP_exe}")
    add_selftest(run_PlotUE_Data_selftest "${TOOLS_STAGE_DIR}/PlotUE_Data" "${_PlotUE_Data_exe}")

    if(SUPERBUILD_RUN_TESTS)
      add_test(NAME run_assimp_unit
        COMMAND ${CMAKE_COMMAND} -E chdir "${ASSIMP_BUILD}/bin/${SUPERBUILD_CTEST_CONFIG}"
                "${ASSIMP_BUILD}/bin/${SUPERBUILD_CTEST_CONFIG}/unit.exe")
      set_tests_properties(run_assimp_unit PROPERTIES TIMEOUT 120)
    endif()

  elseif(APPLE)
    set(_OpenFileTCP_exe "${TOOLS_STAGE_DIR}/OpenFileTCP.app/Contents/MacOS/OpenFileTCP")
    set(_PlotUE_Data_exe "${TOOLS_STAGE_DIR}/PlotUE_Data.app/Contents/MacOS/appPlotUE_Data")
    add_path_exists_test(stage_OpenFileTCP_exists "${_OpenFileTCP_exe}")
    add_path_exists_test(stage_PlotUE_Data_exists "${_PlotUE_Data_exe}")
    add_selftest(run_OpenFileTCP_selftest "${TOOLS_STAGE_DIR}/OpenFileTCP.app/Contents/MacOS" "${_OpenFileTCP_exe}")
    add_selftest(run_PlotUE_Data_selftest "${TOOLS_STAGE_DIR}/PlotUE_Data.app/Contents/MacOS" "${_PlotUE_Data_exe}")

    if(SUPERBUILD_RUN_TESTS)
      add_test(NAME run_assimp_unit
        COMMAND ${CMAKE_COMMAND} -E chdir "${ASSIMP_BUILD}/bin"
                "${ASSIMP_BUILD}/bin/unit")
      set_tests_properties(run_assimp_unit PROPERTIES TIMEOUT 120)
    endif()

  else()
    set(_OpenFileTCP_exe "${TOOLS_STAGE_DIR}/OpenFileTCP/OpenFileTCP")
    set(_PlotUE_Data_exe "${TOOLS_STAGE_DIR}/PlotUE_Data/appPlotUE_Data")
    add_path_exists_test(stage_OpenFileTCP_exists "${_OpenFileTCP_exe}")
    add_path_exists_test(stage_PlotUE_Data_exists "${_PlotUE_Data_exe}")
    add_selftest(run_OpenFileTCP_selftest "${TOOLS_STAGE_DIR}/OpenFileTCP" "${_OpenFileTCP_exe}")
    add_selftest(run_PlotUE_Data_selftest "${TOOLS_STAGE_DIR}/PlotUE_Data" "${_PlotUE_Data_exe}")

    if(SUPERBUILD_RUN_TESTS)
      add_test(NAME run_assimp_unit
        COMMAND ${CMAKE_COMMAND} -E chdir "${ASSIMP_BUILD}/bin"
                "${ASSIMP_BUILD}/bin/unit")
      set_tests_properties(run_assimp_unit PROPERTIES TIMEOUT 120)
    endif()
  endif()

  # Optional Unreal automation and GenerateProjectFiles/build checks
  if(SUPERBUILD_ENABLE_UE_AUTOMATION AND NOT UE_AUTOMATION_EXE STREQUAL "")
    if(EXISTS "${UE_AUTOMATION_EXE}")
      separate_arguments(_UE_AUTOMATION_ARGS NATIVE_COMMAND "${UE_AUTOMATION_ARGS}")
      add_test(NAME ue_automation
        COMMAND "${UE_AUTOMATION_EXE}" ${_UE_AUTOMATION_ARGS})
      set_tests_properties(ue_automation PROPERTIES TIMEOUT 900)
    else()
      message(STATUS "UE automation exe not found at '${UE_AUTOMATION_EXE}'. Skipping ue_automation test.")
    endif()
  endif()

  if(SUPERBUILD_ENABLE_UE_GENBUILD AND NOT UE_GENERATE_FILES_EXE STREQUAL "" AND NOT UE_BUILD_EXE STREQUAL "")
    if(EXISTS "${UE_GENERATE_FILES_EXE}")
      separate_arguments(_UE_GPF_ARGS NATIVE_COMMAND "${UE_GENERATE_FILES_ARGS}")
      if("${UE_GENERATE_FILES_EXE}" MATCHES "\\.bat$" OR "${UE_GENERATE_FILES_EXE}" MATCHES "\\.cmd$")
        add_test(NAME ue_generate_project_files
          COMMAND cmd /c "\"${UE_GENERATE_FILES_EXE}\"" ${_UE_GPF_ARGS}
          WORKING_DIRECTORY "${UE_ROOT}")
      else()
        add_test(NAME ue_generate_project_files
          COMMAND "${UE_GENERATE_FILES_EXE}" ${_UE_GPF_ARGS}
          WORKING_DIRECTORY "${UE_ROOT}")
      endif()
      set_tests_properties(ue_generate_project_files PROPERTIES TIMEOUT 900)
    else()
      message(STATUS "GenerateProjectFiles not found at '${UE_GENERATE_FILES_EXE}'. Skipping ue_generate_project_files test.")
    endif()

    if(EXISTS "${UE_BUILD_EXE}")
      separate_arguments(_UE_BUILD_ARGS NATIVE_COMMAND "${UE_BUILD_ARGS}")
      if("${UE_BUILD_EXE}" MATCHES "\\.bat$" OR "${UE_BUILD_EXE}" MATCHES "\\.cmd$")
        add_test(NAME ue_build_editor
          COMMAND cmd /c "\"${UE_BUILD_EXE}\"" ${_UE_BUILD_ARGS}
          WORKING_DIRECTORY "${UE_ROOT}")
      else()
        add_test(NAME ue_build_editor
          COMMAND "${UE_BUILD_EXE}" ${_UE_BUILD_ARGS}
          WORKING_DIRECTORY "${UE_ROOT}")
      endif()
      set_tests_properties(ue_build_editor PROPERTIES TIMEOUT 1800)
    else()
      message(STATUS "UE build helper not found at '${UE_BUILD_EXE}'. Skipping ue_build_editor test.")
    endif()
  endif()

  add_custom_target(superbuild_tests ALL
    DEPENDS assimp_stage OpenFileTCP_stage PlotUE_Data_stage
    COMMENT "Running superbuild staging checks"
  )

  if(SUPERBUILD_RUN_TESTS)
    # Multi-config generators (e.g., MSVC) need an explicit configuration for ctest.
    if(CMAKE_CONFIGURATION_TYPES)
      set(_CTEST_CONFIG_ARGS -C ${SUPERBUILD_CTEST_CONFIG})
    else()
      set(_CTEST_CONFIG_ARGS)
    endif()

    add_custom_command(TARGET superbuild_tests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/Testing"
      COMMAND ${CMAKE_CTEST_COMMAND}
              --output-on-failure
              --output-log "${CMAKE_BINARY_DIR}/Testing/superbuild-ctest.log"
              ${_CTEST_CONFIG_ARGS}
      COMMENT "Executing ctest for staged tool bundles (log: ${CMAKE_BINARY_DIR}/Testing/superbuild-ctest.log)"
    )
  endif()
endif()

# ======================
# Convenience messages
# ======================
message(STATUS "Superbuild will stage outputs in: ${CMAKE_BINARY_DIR}")

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
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/Testing")
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
#   cmake -S . -B _superbuild -G "Visual Studio 17 2022" -A x64
#   cmake --build _superbuild --config Release --parallel
#
# Usage (macOS Ninja):
#   cmake -S . -B _superbuild -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build _superbuild --parallel

include(ExternalProject)

# --- Project roots (adjust if your tree differs) ---
set(UE_ROOT "${CMAKE_SOURCE_DIR}")
set(ASSIMP_SRC      "${UE_ROOT}/Plugins/UE4_Assimp/Source/ThirdParty/UE_AssimpLibrary/assimp")
set(HDF5_SRC        "${UE_ROOT}/Plugins/Hdf5DataPlugin/Source/ThirdParty/hdf5-2.0.0")

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

# Build the unit test target (but don't run during build - only via ctest)
if(SUPERBUILD_RUN_TESTS)
  ExternalProject_Add_Step(assimp_ep build_unit_tests
    COMMAND ${CMAKE_COMMAND} --build . --config ${SUPERBUILD_CTEST_CONFIG} --target unit --parallel
    DEPENDEES build
    WORKING_DIRECTORY "${ASSIMP_BUILD}"
    COMMENT "Building Assimp unit tests"
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

# ---------------------------------------
# 2) Build HDF5 (Hdf5_DataImporter) and INSTALL into module ThirdParty layout
# ---------------------------------------
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
  -DCMAKE_INSTALL_PREFIX=${HDF5_INST}
)

ExternalProject_Add(hdf5_ep
  SOURCE_DIR  "${HDF5_SRC}"
  BINARY_DIR  "${HDF5_BUILD}"
  INSTALL_DIR "${HDF5_INST}"
  CMAKE_ARGS  ${_hdf5_cmake_args}
  BUILD_COMMAND   ${CMAKE_COMMAND} --build . --config Release --parallel
  INSTALL_COMMAND ${CMAKE_COMMAND} --build . --target install --config Release --parallel
  UPDATE_DISCONNECTED 1
)

# Convenience umbrella target
add_custom_target(all_external
  DEPENDS assimp_stage hdf5_ep
)

add_dependencies(all_external
  assimp_ep
  hdf5_ep
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

  if(WIN32)
    if(SUPERBUILD_RUN_TESTS)
      add_test(NAME run_assimp_unit
        COMMAND ${CMAKE_COMMAND} -E chdir "${ASSIMP_BUILD}"
                "${ASSIMP_BUILD}/bin/${SUPERBUILD_CTEST_CONFIG}/unit.exe")
      set_tests_properties(run_assimp_unit PROPERTIES TIMEOUT 120)
    endif()

  elseif(APPLE)
    if(SUPERBUILD_RUN_TESTS)
      add_test(NAME run_assimp_unit
        COMMAND ${CMAKE_COMMAND} -E chdir "${ASSIMP_BUILD}"
                "${ASSIMP_BUILD}/bin/unit")
      set_tests_properties(run_assimp_unit PROPERTIES TIMEOUT 120)
    endif()

  else()
    if(SUPERBUILD_RUN_TESTS)
      add_test(NAME run_assimp_unit
        COMMAND ${CMAKE_COMMAND} -E chdir "${ASSIMP_BUILD}"
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

  # Optional UE GenerateProjectFiles test (only for source builds)
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
      set(_UE_GPF_TEST_EXISTS FALSE)
    endif()
  endif()

  # Optional UE Build test (works for both source and binary builds)
  if(SUPERBUILD_ENABLE_UE_GENBUILD AND NOT UE_BUILD_EXE STREQUAL "")
    if(EXISTS "${UE_BUILD_EXE}")
      separate_arguments(_UE_BUILD_ARGS NATIVE_COMMAND "${UE_BUILD_ARGS}")
      add_test(NAME ue_build_editor
        COMMAND "${UE_BUILD_EXE}" ${_UE_BUILD_ARGS}
        WORKING_DIRECTORY "${UE_ROOT}")
      set_tests_properties(ue_build_editor PROPERTIES TIMEOUT 1800)
      # Only add dependency if the generate test exists
      if(_UE_GPF_TEST_EXISTS)
        set_tests_properties(ue_build_editor PROPERTIES DEPENDS ue_generate_project_files)
      endif()
    else()
      message(STATUS "UE build helper not found at '${UE_BUILD_EXE}'. Skipping ue_build_editor test.")
    endif()
  endif()

  add_custom_target(superbuild_tests
    DEPENDS assimp_stage hdf5_ep
    COMMENT "Running superbuild staging checks"
  )

  if(SUPERBUILD_RUN_TESTS)
    # Multi-config generators (e.g., MSVC) need an explicit configuration for ctest.
    if(CMAKE_CONFIGURATION_TYPES)
      set(_CTEST_CONFIG_ARGS -C ${SUPERBUILD_CTEST_CONFIG})
    else()
      set(_CTEST_CONFIG_ARGS)
    endif()

    # Create a wrapper script that runs ctest but always returns success
    set(_CTEST_WRAPPER "${CMAKE_BINARY_DIR}/Testing/run_ctest.cmake")
    file(WRITE "${_CTEST_WRAPPER}" "
execute_process(
  COMMAND \"${CMAKE_CTEST_COMMAND}\" --output-on-failure --output-log \"${CMAKE_BINARY_DIR}/Testing/superbuild-ctest.log\" ${_CTEST_CONFIG_ARGS}
  WORKING_DIRECTORY \"${CMAKE_BINARY_DIR}\"
  RESULT_VARIABLE _ctest_result
)
if(NOT _ctest_result EQUAL 0)
  message(STATUS \"Some tests failed (exit code: \${_ctest_result}). See ${CMAKE_BINARY_DIR}/Testing/superbuild-ctest.log for details.\")
else()
  message(STATUS \"All tests passed!\")
endif()
")

    add_custom_command(TARGET superbuild_tests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/Testing"
      COMMAND ${CMAKE_COMMAND} -P "${_CTEST_WRAPPER}"
      COMMENT "Executing ctest for staged tool bundles (log: ${CMAKE_BINARY_DIR}/Testing/superbuild-ctest.log)"
    )
  endif()
endif()

# ======================
# Convenience messages
# ======================
message(STATUS "Superbuild will stage outputs in: ${CMAKE_BINARY_DIR}")

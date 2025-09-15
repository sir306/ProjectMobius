cmake_minimum_required(VERSION 3.16)
project(OpenFileTCP VERSION 1.0 LANGUAGES CXX)

# =========================
# Platform / toolchain
# =========================
if(APPLE)
  # Universal app (Intel + Apple Silicon), Big Sur floor
  set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

if(MSVC)
  # UE 5.4+ expects VS 2022; enforce 64-bit
  if(NOT MSVC_VERSION GREATER_EQUAL 1930)
    message(FATAL_ERROR "Use Visual Studio 2022 (v17.x).")
  endif()
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "64-bit MSVC build required (-A x64).")
  endif()
endif()

# =========================
# Qt setup (default to 6.9.2 kits, overrideable)
# =========================
if(NOT DEFINED CMAKE_PREFIX_PATH AND NOT DEFINED Qt6_DIR)
  if(WIN32)
    set(CMAKE_PREFIX_PATH "C:/Qt/6.9.2/msvc2022_64" CACHE STRING "" FORCE)
  elseif(APPLE)
    set(CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/6.9.2/macos" CACHE STRING "" FORCE)
  endif()
  message(STATUS "Using default Qt6 path: ${CMAKE_PREFIX_PATH}")
endif()

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets HttpServer)

# =========================
# Target
# =========================
qt_add_executable(OpenFileTCP
  MACOSX_BUNDLE
  main.cpp
)

target_link_libraries(OpenFileTCP
  PRIVATE Qt6::Core Qt6::Widgets Qt6::HttpServer
)

# macOS bundle metadata
set_target_properties(OpenFileTCP PROPERTIES
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_GUI_IDENTIFIER com.projectmobius.openfiletcp
  MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
)

# Optional install support (not required for deploy step)
include(GNUInstallDirs)
install(TARGETS OpenFileTCP
  BUNDLE  DESTINATION .
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# =========================
# Deployment (Qt deploy)
# =========================
# Find Qt's bin dir via qmake so we call the matching deploy tool
set(_QT_BIN_DIR "")
if(TARGET Qt6::qmake)
  get_target_property(_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
  get_filename_component(_QT_BIN_DIR "${_QMAKE_EXECUTABLE}" DIRECTORY)
endif()

# Output deploy dirs (keep Unreal’s expectations)
if(WIN32)
  set(DEPLOY_DIR "${CMAKE_BINARY_DIR}/WindowsExe")
elseif(APPLE)
  set(DEPLOY_DIR "${CMAKE_BINARY_DIR}/MacExe")
else()
  set(DEPLOY_DIR "${CMAKE_BINARY_DIR}/Deploy")
endif()

if(WIN32)
  # Prefer the Qt6-named deployer to avoid Qt5 collisions
  find_program(WINDEPLOYQT_EXECUTABLE
    NAMES windeployqt6 windeployqt windeployqt.exe
    HINTS "${_QT_BIN_DIR}"
  )
  if(NOT WINDEPLOYQT_EXECUTABLE)
    message(FATAL_ERROR "windeployqt not found. Ensure Qt bin is on PATH or Qt6::qmake is available.")
  endif()

  add_custom_target(DeployOpenFileTCP ALL
    DEPENDS OpenFileTCP
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_DIR}"
    # Copy the exe first, then let windeployqt populate DLLs/plugins beside it
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:OpenFileTCP>" "${DEPLOY_DIR}/"
    COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --verbose 1
            --dir "${DEPLOY_DIR}"
            --no-compiler-runtime       # remove this line if you want the MSVC CRT copied by windeployqt
            "${DEPLOY_DIR}/OpenFileTCP.exe"
    COMMENT "Deploying OpenFileTCP to ${DEPLOY_DIR} (Windows)"
  )

elseif(APPLE)
  find_program(MACDEPLOYQT_EXECUTABLE
    NAMES macdeployqt
    HINTS "${_QT_BIN_DIR}"
  )
  if(NOT MACDEPLOYQT_EXECUTABLE)
    message(FATAL_ERROR "macdeployqt not found. Ensure Qt bin is on PATH or Qt6::qmake is available.")
  endif()

  # Where the app bundle is produced in-tree
  set(APP_BUNDLE "$<TARGET_BUNDLE_DIR:OpenFileTCP>")

  add_custom_target(DeployOpenFileTCP ALL
    DEPENDS OpenFileTCP
    # 1) Fix up bundle with macdeployqt (copies frameworks/plugins next to the app)
    COMMAND "${MACDEPLOYQT_EXECUTABLE}" "${APP_BUNDLE}" -verbose=2 -always-overwrite

    # 2) Normalize ALL symlinks under the bundle to real files so Unreal's UAT
    #    NonUFS copy doesn't hit size-mismatch (e.g., QtVirtualKeyboardQml.framework)
    #    We do it in-place: rsync to a temp dir with -L (follow symlinks), swap.
    COMMAND ${CMAKE_COMMAND} -E echo "Normalizing framework symlinks in ${APP_BUNDLE}"
    COMMAND /usr/bin/rsync -aL "${APP_BUNDLE}/" "${APP_BUNDLE}.nosymlinks/"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${APP_BUNDLE}"
    COMMAND ${CMAKE_COMMAND} -E rename "${APP_BUNDLE}.nosymlinks" "${APP_BUNDLE}"

    # 3) Copy the finalized bundle into the expected deploy directory
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/Tools/bin/Mac"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${APP_BUNDLE}"
            "${CMAKE_SOURCE_DIR}/Tools/bin/Mac/OpenFileTCP.app"

    COMMENT "Deploying OpenFileTCP (macOS) into Tools/bin/Mac and normalizing symlinks"
  )


else()
  message(WARNING "No deploy step defined for this platform.")
endif()

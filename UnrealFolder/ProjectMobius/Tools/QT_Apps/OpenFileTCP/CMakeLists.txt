cmake_minimum_required(VERSION 3.16)
project(OpenFileTCP VERSION 1.0 LANGUAGES CXX)

# ---------- Platform setup ----------
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

if(MSVC)
  if(NOT MSVC_VERSION GREATER_EQUAL 1930)
    message(FATAL_ERROR "Use Visual Studio 2022 (v17.x).")
  endif()
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "64-bit MSVC build required (-A x64).")
  endif()
endif()

# ---------- Qt & C++ ----------
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Qt 6.9.2 kits unless caller passes their own CMAKE_PREFIX_PATH/Qt6_DIR
if(NOT DEFINED CMAKE_PREFIX_PATH AND NOT DEFINED Qt6_DIR)
  if(WIN32)
    set(CMAKE_PREFIX_PATH "C:/Qt/6.9.2/msvc2022_64" CACHE STRING "" FORCE)
  elseif(APPLE)
    set(CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/6.9.2/macos" CACHE STRING "" FORCE)
  endif()
  message(STATUS "CMAKE_PREFIX_PATH = ${CMAKE_PREFIX_PATH}")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets HttpServer)

qt_add_executable(OpenFileTCP
  MACOSX_BUNDLE
  main.cpp
)

target_link_libraries(OpenFileTCP
  PRIVATE Qt6::Core Qt6::Widgets Qt6::HttpServer
)

set_target_properties(OpenFileTCP PROPERTIES
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_GUI_IDENTIFIER com.projectmobius.openfiletcp
  MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
)

include(GNUInstallDirs)
install(TARGETS OpenFileTCP
  BUNDLE  DESTINATION .
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ---------- Deploy ----------
# Find the matching Qt bin dir via qmake
set(_QT_BIN_DIR "")
if(TARGET Qt6::qmake)
  get_target_property(_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
  get_filename_component(_QT_BIN_DIR "${_QMAKE_EXECUTABLE}" DIRECTORY)
endif()

# Our desired deploy output folders
if(WIN32)
  set(DEPLOY_DIR "${CMAKE_BINARY_DIR}/WindowsExe")
elseif(APPLE)
  set(DEPLOY_DIR "${CMAKE_BINARY_DIR}/MacOS")
else()
  set(DEPLOY_DIR "${CMAKE_BINARY_DIR}/Deploy")
endif()

if(WIN32)
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
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:OpenFileTCP>" "${DEPLOY_DIR}/"
    COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --verbose 1
            --dir "${DEPLOY_DIR}"
            --no-compiler-runtime     # remove this line if you want the MSVC CRT copied
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

  add_custom_target(DeployOpenFileTCP ALL
    DEPENDS OpenFileTCP
    COMMAND "${MACDEPLOYQT_EXECUTABLE}"
            "$<TARGET_BUNDLE_DIR:OpenFileTCP>"
            -verbose=2
            -always-overwrite
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_BUNDLE_DIR:OpenFileTCP>"
            "${DEPLOY_DIR}/OpenFileTCP.app"
    COMMENT "Deploying OpenFileTCP to ${DEPLOY_DIR} (macOS)"
  )
else()
  message(WARNING "No deploy step for this platform.")
endif()

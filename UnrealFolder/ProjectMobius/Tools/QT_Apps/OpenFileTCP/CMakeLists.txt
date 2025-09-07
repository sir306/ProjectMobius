cmake_minimum_required(VERSION 3.16)

project(OpenFileTCP VERSION 1.0 LANGUAGES CXX)

# -----------------------------
# Qt & C++ setup
# -----------------------------
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets HttpServer)

# -----------------------------
# Windows: enforce 64-bit build
# -----------------------------
if(WIN32)
  # CMAKE_SIZEOF_VOID_P == 8 => 64-bit; 4 => 32-bit
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
      "This project requires a 64-bit Windows build. "
      "Regenerate with a 64-bit generator (e.g., -A x64 for MSVC).")
  endif()
endif()

# -----------------------------
# macOS: build universal binary
# -----------------------------
if(APPLE)
  # Build one app that runs on Intel and Apple Silicon
  set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Build universal (Intel + Apple Silicon)" FORCE)
  # Big Sur is a safe floor for Apple Silicon; adjust only if you need older macOS
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

# -----------------------------
# App target
# -----------------------------
qt_add_executable(OpenFileTCP
  MACOSX_BUNDLE
  main.cpp
)

target_link_libraries(OpenFileTCP
  PRIVATE Qt6::Core Qt6::Widgets Qt6::HttpServer
)

# Bundle metadata (macOS niceties)
set_target_properties(OpenFileTCP PROPERTIES
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_GUI_IDENTIFIER com.projectmobius.openfiletcp
  MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
)

# Optional: install support (lets you do `cmake --install`)
include(GNUInstallDirs)
install(TARGETS OpenFileTCP
  BUNDLE  DESTINATION .
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}  # used on Windows/Linux
)

# =========================
# Deployment (Qt deploy)
# =========================
# Resolve Qt's bin dir via Qt6::qmake so we call the matching deploy tool
if(TARGET Qt6::qmake)
  get_target_property(_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
  get_filename_component(_QT_BIN_DIR "${_QMAKE_EXECUTABLE}" DIRECTORY)
endif()

# Output folder for deployed artifacts
if(WIN32)
  set(DEPLOY_DIR "${CMAKE_BINARY_DIR}/WindowsExe")
elseif(APPLE)
  set(DEPLOY_DIR "${CMAKE_BINARY_DIR}/MacExe")
else()
  set(DEPLOY_DIR "${CMAKE_BINARY_DIR}/Deploy")
endif()

# Find the deploy tool for the current platform
if(WIN32)
  find_program(WINDEPLOYQT_EXECUTABLE
    NAMES windeployqt windeployqt.exe
    HINTS "${_QT_BIN_DIR}"
  )
  if(NOT WINDEPLOYQT_EXECUTABLE)
    message(FATAL_ERROR "windeployqt not found. Ensure Qt bin is on PATH or Qt6::qmake is available.")
  endif()

  add_custom_target(DeployOpenFileTCP ALL
    DEPENDS OpenFileTCP
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_DIR}"
    # Copy built exe to deploy dir first
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:OpenFileTCP>" "${DEPLOY_DIR}/"
    # Run windeployqt to gather required DLLs/plugins into DEPLOY_DIR
    COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --verbose 1
            --dir "${DEPLOY_DIR}"
            --no-compiler-runtime
            --no-angle
            "${DEPLOY_DIR}/OpenFileTCP.exe"
    COMMENT "Deploying OpenFileTCP (Windows 64-bit) into ${DEPLOY_DIR}"
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
    # Fix up the in-tree bundle (universal-aware if the app was built universal)
    COMMAND "${MACDEPLOYQT_EXECUTABLE}"
            "$<TARGET_BUNDLE_DIR:OpenFileTCP>"
            -verbose=2
            -always-overwrite
    # Copy the fully-deployed .app into MacExe for distribution
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_BUNDLE_DIR:OpenFileTCP>"
            "${DEPLOY_DIR}/OpenFileTCP.app"
    COMMENT "Deploying OpenFileTCP (universal x86_64+arm64) into ${DEPLOY_DIR}"
  )

else()
  message(WARNING "No deploy step defined for this platform; only Windows/macOS are handled.")
endif()

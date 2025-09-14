cmake_minimum_required(VERSION 3.16)

project(PlotUE_Data VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)

find_package(Qt6 REQUIRED COMPONENTS Quick WebSockets Graphs)

qt_standard_project_setup(REQUIRES 6.9)

qt_add_executable(appPlotUE_Data
    main.cpp
)

qt_add_qml_module(appPlotUE_Data
    URI PlotUE_Data
    VERSION 1.0
    QML_FILES
    AppWindow.qml
    SOURCES WebSocketManager.h WebSocketManager.cpp
    SOURCES ChartTableModel.h ChartTableModel.cpp
        SOURCES AxisSettings.h
        SOURCES ChartSettings.h
        SOURCES MessageProcessor.h MessageProcessor.cpp
        QML_FILES GraphHoverItem.qml
        RESOURCES shaders/PlaybarShaderEffect.frag
        RESOURCES shaders/PlaybarShaderEffect.frag.qsb
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appPlotUE_Data PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appPlotUE_Data
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appPlotUE_Data
    PRIVATE Qt6::Quick
    PRIVATE Qt6::WebSockets
    PRIVATE Qt6::Graphs
)

include(GNUInstallDirs)
install(TARGETS appPlotUE_Data
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# -----------------------
# Deployment (Qt deploy)
# -----------------------

# Find Qt bin (so we call the matching deploy tool)
set(_QT_BIN_DIR "")
if(TARGET Qt6::qmake)
  get_target_property(_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
  get_filename_component(_QT_BIN_DIR "${_QMAKE_EXECUTABLE}" DIRECTORY)
endif()

# Where we want the deployed artifacts placed by this subproject:
if(WIN32)
  set(PLOT_DEPLOY_DIR "${CMAKE_BINARY_DIR}/WindowsExe")
elseif(APPLE)
  set(PLOT_DEPLOY_DIR "${CMAKE_BINARY_DIR}/MacExe")
else()
  set(PLOT_DEPLOY_DIR "${CMAKE_BINARY_DIR}/Deploy")
endif()

if(WIN32)
  # Find the deployer
  find_program(WINDEPLOYQT_EXECUTABLE
    NAMES windeployqt6 windeployqt windeployqt.exe
    HINTS "${_QT_BIN_DIR}"
  )
  if(NOT WINDEPLOYQT_EXECUTABLE)
    message(FATAL_ERROR "windeployqt not found. Ensure Qt bin is on PATH or Qt6::qmake target exists.")
  endif()

  add_custom_target(DeployPlotUE_Data ALL
    DEPENDS appPlotUE_Data
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PLOT_DEPLOY_DIR}"
    # Put exe in deploy dir first, then let windeployqt populate DLLs & QML
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:appPlotUE_Data>" "${PLOT_DEPLOY_DIR}/"
    # VERY IMPORTANT: give windeployqt the QML dir so it pulls Quick, Graphs, WebSockets, etc.
    COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --verbose 1
            --dir "${PLOT_DEPLOY_DIR}"
            --qmldir "${CMAKE_CURRENT_SOURCE_DIR}"
            --plugindir "${PLOT_DEPLOY_DIR}/plugins"
            --no-compiler-runtime
            "${PLOT_DEPLOY_DIR}/appPlotUE_Data.exe"
    COMMENT "Deploying PlotUE_Data to ${PLOT_DEPLOY_DIR} (Windows)"
  )
  
  # Copy shaders if directory exists (Windows)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders")
    add_custom_command(TARGET DeployPlotUE_Data POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${CMAKE_CURRENT_SOURCE_DIR}/Shaders"
              "${PLOT_DEPLOY_DIR}/Shaders"
      COMMENT "Copying Shaders directory (Windows)"
    )
  endif()

elseif(APPLE)
  find_program(MACDEPLOYQT_EXECUTABLE
    NAMES macdeployqt
    HINTS "${_QT_BIN_DIR}"
  )
  if(NOT MACDEPLOYQT_EXECUTABLE)
    message(FATAL_ERROR "macdeployqt not found.")
  endif()

  add_custom_target(DeployPlotUE_Data ALL
    DEPENDS appPlotUE_Data
    # macdeployqt operates on the app bundle in the build dir
    COMMAND "${MACDEPLOYQT_EXECUTABLE}" "$<TARGET_BUNDLE_DIR:appPlotUE_Data>" -verbose=1 -qmldir="${CMAKE_CURRENT_SOURCE_DIR}"
    # Normalize symlinks so Unreal packaging doesn't trip over framework links
    COMMAND ${CMAKE_COMMAND} -E echo "Normalizing framework symlinks in $<TARGET_BUNDLE_DIR:appPlotUE_Data>"
    COMMAND /usr/bin/rsync -aL "$<TARGET_BUNDLE_DIR:appPlotUE_Data>/" "$<TARGET_BUNDLE_DIR:appPlotUE_Data>.nosymlinks/"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_BUNDLE_DIR:appPlotUE_Data>"
    COMMAND ${CMAKE_COMMAND} -E rename "$<TARGET_BUNDLE_DIR:appPlotUE_Data>.nosymlinks" "$<TARGET_BUNDLE_DIR:appPlotUE_Data>"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PLOT_DEPLOY_DIR}"
COMMAND ${CMAKE_COMMAND} -E copy_directory
        "$<TARGET_BUNDLE_DIR:appPlotUE_Data>"
        "${PLOT_DEPLOY_DIR}/appPlotUE_Data.app"
    COMMENT "Deploying PlotUE_Data app bundle (macOS) and normalizing symlinks"
  )
  
  # Copy shaders if directory exists (macOS)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders")
    add_custom_command(TARGET DeployPlotUE_Data POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${CMAKE_CURRENT_SOURCE_DIR}/Shaders"
              "$<TARGET_BUNDLE_DIR:appPlotUE_Data>/Contents/Resources/Shaders"
      COMMENT "Copying Shaders directory to app bundle (macOS)"
    )
  endif()
endif()
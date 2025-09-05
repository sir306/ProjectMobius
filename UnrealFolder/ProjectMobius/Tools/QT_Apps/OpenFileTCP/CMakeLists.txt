cmake_minimum_required(VERSION 3.16)

project(OpenFileTCP LANGUAGES CXX)

cmake_minimum_required(VERSION 3.16)
project(OpenFileTCP VERSION 1.0 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets HttpServer)

# Build a .app bundle on macOS
qt_add_executable(OpenFileTCP
    MACOSX_BUNDLE
    main.cpp
)

target_link_libraries(OpenFileTCP
    PRIVATE Qt6::Core Qt6::Widgets Qt6::HttpServer
)

# Nice-to-have bundle metadata
set_target_properties(OpenFileTCP PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER com.projectmobius.openfiletcp
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    # If you have an icon:
    # MACOSX_BUNDLE_ICON_FILE AppIcon
    # RESOURCE "${CMAKE_SOURCE_DIR}/icons/AppIcon.icns"
)

# Install the bundle (BUNDLE is key for macOS)
include(GNUInstallDirs)
install(TARGETS OpenFileTCP
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}  # for non-mac builds
)

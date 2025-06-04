cmake_minimum_required(VERSION 3.16)

project(OpenFileTCP LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find both Core, Widgets, and HttpServer components
find_package(QT NAMES Qt6 REQUIRED COMPONENTS Core Widgets HttpServer)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets HttpServer)

# Add your sources here (add more if needed)
add_executable(OpenFileTCP
  main.cpp
)

# Link to required Qt modules
target_link_libraries(OpenFileTCP
  Qt${QT_VERSION_MAJOR}::Core
  Qt${QT_VERSION_MAJOR}::Widgets
  Qt${QT_VERSION_MAJOR}::HttpServer
)

# Install targets
include(GNUInstallDirs)
install(TARGETS OpenFileTCP
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

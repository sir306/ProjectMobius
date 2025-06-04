# Install script for directory: C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/hl/src

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files/HDF_Group/HDF5/1.14.4")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "hlheaders" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE FILES
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/hl/src/H5DOpublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/hl/src/H5DSpublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/hl/src/H5IMpublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/hl/src/H5LTpublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/hl/src/H5PTpublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/hl/src/H5TBpublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/hl/src/H5LDpublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/hl/src/hdf5_hl.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "hllibraries" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE FILE OPTIONAL FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/Debug/hdf5_hl_D.pdb")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE FILE OPTIONAL FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/RelWithDebInfo/hdf5_hl.pdb")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "hllibraries" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE FILE OPTIONAL FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/Debug/.pdb")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE FILE OPTIONAL FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/RelWithDebInfo/.pdb")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "hllibraries" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/Debug/libhdf5_hl_D.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/Release/libhdf5_hl.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/MinSizeRel/libhdf5_hl.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/RelWithDebInfo/libhdf5_hl.lib")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "hllibraries" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY OPTIONAL FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/Debug/hdf5_hl_D.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY OPTIONAL FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/Release/hdf5_hl.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY OPTIONAL FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/MinSizeRel/hdf5_hl.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY OPTIONAL FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/RelWithDebInfo/hdf5_hl.lib")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "hllibraries" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE SHARED_LIBRARY FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/Debug/hdf5_hl_D.dll")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE SHARED_LIBRARY FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/Release/hdf5_hl.dll")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE SHARED_LIBRARY FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/MinSizeRel/hdf5_hl.dll")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE SHARED_LIBRARY FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/RelWithDebInfo/hdf5_hl.dll")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "hllibraries" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig" TYPE FILE FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/CMakeFiles/hdf5_hl.pc")
endif()


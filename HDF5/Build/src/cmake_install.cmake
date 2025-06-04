# Install script for directory: C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src

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

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/src/H5FDsubfiling/cmake_install.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "headers" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE FILES
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/hdf5.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5api_adpt.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5encode.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5public.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5Apublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5ACpublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5Cpublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5Dpublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5Epubgen.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5Epublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5ESdevelop.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5ESpublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5Fpublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5FDcore.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5FDdevelop.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5FDdirect.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5FDfamily.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5FDhdfs.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5FDlog.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5FDmirror.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5FDmpi.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5FDmpio.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5FDmulti.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5FDonion.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5FDpublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5FDros3.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5FDs3comms.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5FDsec2.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5FDsplitter.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5FDstdio.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5FDwindows.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5FDsubfiling/H5FDsubfiling.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5FDsubfiling/H5FDioc.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5Gpublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5Idevelop.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5Ipublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5Ldevelop.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5Lpublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5Mpublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5MMpublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5Opublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5Ppublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5PLextern.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5PLpublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5Rpublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5Spublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5Tdevelop.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5Tpublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5TSdevelop.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5VLconnector.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5VLconnector_passthru.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5VLnative.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5VLpassthru.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5VLpublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5Zdevelop.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5Zpublic.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5Epubgen.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5version.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/hdf5-1.14.4-2/hdf5-1.14.4-2/src/H5overflow.h"
    "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/src/H5pubconf.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "libraries" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE FILE OPTIONAL FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/Debug/hdf5_D.pdb")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE FILE OPTIONAL FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/RelWithDebInfo/hdf5.pdb")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "libraries" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE FILE OPTIONAL FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/Debug/.pdb")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE FILE OPTIONAL FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/RelWithDebInfo/.pdb")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "libraries" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/Debug/libhdf5_D.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/Release/libhdf5.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/MinSizeRel/libhdf5.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/RelWithDebInfo/libhdf5.lib")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "libraries" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY OPTIONAL FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/Debug/hdf5_D.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY OPTIONAL FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/Release/hdf5.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY OPTIONAL FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/MinSizeRel/hdf5.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY OPTIONAL FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/RelWithDebInfo/hdf5.lib")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "libraries" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE SHARED_LIBRARY FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/Debug/hdf5_D.dll")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE SHARED_LIBRARY FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/Release/hdf5.dll")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE SHARED_LIBRARY FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/MinSizeRel/hdf5.dll")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE SHARED_LIBRARY FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/bin/RelWithDebInfo/hdf5.dll")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "libraries" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig" TYPE FILE FILES "C:/Users/User_VR4/Desktop/WORK/ProjectMobius/ProjectMobius/HDF5/Build/CMakeFiles/hdf5.pc")
endif()


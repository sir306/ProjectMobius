# Builds write_mobius_h5 against an installed HDF5, normally the copy you built
# from the HDF5 source bundled in this repository:
#
#   cmake -S . -B build -DCMAKE_PREFIX_PATH=<your HDF5 install prefix>
#   cmake --build build --config Release
#
# If CMake cannot find HDF5, point it at the folder holding hdf5-config.cmake
# instead: -DHDF5_DIR=<prefix>/cmake on Windows (search the prefix for that file
# on Linux/macOS).
#
# See docs/HDF5-EXPORT-GUIDE.md for building HDF5 itself.

cmake_minimum_required(VERSION 3.21)
project(mobius_h5_writer_example C)

find_package(HDF5 CONFIG REQUIRED)

add_executable(write_mobius_h5 write_mobius_h5.c)
set_target_properties(write_mobius_h5 PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)

# The bundled HDF5 exports un-namespaced targets (hdf5-static / hdf5-shared);
# some package managers add an hdf5:: namespace. Prefer static so the program
# runs without an HDF5 DLL beside it.
set(_mobius_hdf5_target "")
foreach(_candidate hdf5-static hdf5::hdf5-static hdf5-shared hdf5::hdf5-shared)
  if(TARGET ${_candidate})
    set(_mobius_hdf5_target ${_candidate})
    break()
  endif()
endforeach()
if(NOT _mobius_hdf5_target)
  message(FATAL_ERROR "The HDF5 package found at ${HDF5_DIR} exports none of hdf5-static, hdf5-shared (with or without the hdf5:: namespace).")
endif()
message(STATUS "Linking ${_mobius_hdf5_target}")
target_link_libraries(write_mobius_h5 PRIVATE ${_mobius_hdf5_target})

if(NOT MSVC)
  target_link_libraries(write_mobius_h5 PRIVATE m)
endif()

HDF5 version 1.14.4.2
------------------------------------------------------------------------------

This directory contains the binary (release) distribution of
HDF5 1.14.4 that was compiled on;
    Windows 7 x64, using VISUAL STUDIO ???.

It was built with the following options:
    -- Static and Shared C/C++/Fortran libraries
    -- SZIP (encoder enabled) and ZLIB
    -- HDF5 tools
    -- Java 

The contents of this directory are:

    COPYING                 - Copyright notice
    README.md               - This file
    HDF5-1.14.4-win64.msi    - HDF5 Install Package
    OR
    HDF5-1.14.4-win64.zip    - HDF5 Compressed Package

This binary was built with the ZLIB and SZIP/Libaec external libraries and are
included for convenience.  Libaec is an unrestricted open-source replacement for SZIP
(Encoder ENABLED).


Installation
===========================================================================
1. Execute HDF5-1.14.4-win64.msi
2. Follow prompts
===========================================================================

Uncompress
===========================================================================
1. Extract HDF5-1.14.4-win64.zip
===========================================================================

After Installation
===========================================================================
The examples folder, HDF5Examples, located in the
HDF5 install folder, can be built and tested with CMake and the supplied
HDF5_Examples.cmake file. The HDF5_Examples.cmake expects HDF5 to have
been installed in the default location with above compilers (see the
libhdf5.settings file in the lib install folder). Also, the CMake
utility should be installed.

To test the installation with the examples;
    Create a directory to run the examples.
    Copy HDF5Examples folder to this directory.
    Copy CTestScript.cmake to this directory.
    Copy HDF5_Examples.cmake to this directory.
    Copy HDF5_Examples_options.cmake to this directory.
    The default source folder is defined as "HDF5Examples". It can be changed
        with the CTEST_SOURCE_NAME script option.
    The default installation folder is defined as "C:/Program Files/HDF_Group/HDF5/1.14.4".
        It can be changed with the INSTALLDIR script option.
    The default ctest configuration is defined as "Release". It can be changed
        with the CTEST_CONFIGURATION_TYPE script option. Note that this must
        be the same as the value used with the -C command line option.
    The default build configuration is defined to build and use static libraries.
        Shared libraries can be used with the STATICONLYLIBRARIES script option set to "NO".
    Other options can be changed by editing the HDF5_Examples_options.cmake file.

    If the defaults are okay, execute from this directory:
        ctest -S HDF5_Examples.cmake -C Release -V -O test.log
    If the defaults need change, execute from this directory:
        ctest -S HDF5_Examples.cmake,CTEST_SOURCE_NAME=MyExamples,INSTALLDIR=MyLocation -C Release -V -O test.log

When executed, the ctest script will save the results to the log file, test.log, as
indicated by the ctest command. If you wish to see more build and test information,
add "-VV" to the ctest command. The output should show;
      100% tests passed, 0 tests failed out of 156.

For more information see USING_CMake_Examples.txt in the install folder.
===========================================================================

Documentation for this release can be found at the following URL:
    https://portal.hdfgroup.org/documentation/index.html#hdf5

Bugs should be reported to help@hdfgroup.org.

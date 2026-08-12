# SPDX-License-Identifier: MIT
#
# Builds MobiusIfcBridge -- the extern "C" shim in this directory -- as a SHARED library, and
# installs it to install/{include,lib,bin} in the exact layout the staged UE Build.cs
# (MobiusIfcLibrary.Build.cs, also in this directory) already expects:
#
#   install/include/MobiusIfcBridge.h
#   install/lib/MobiusIfcBridge.lib      (import lib)
#   install/bin/MobiusIfcBridge.dll
#
# NOT BUILT AS PART OF AUTHORING THIS FILE. Nothing in this repository has run `cmake` against
# this CMakeLists.txt -- that is the main thread's job, not this session's (see the task's rules).
# Everything below is written from reading IfcPlusPlus's own CMakeLists.txt and the working
# examples/LoadFileExample/CMakeLists.txt that already links against it successfully (per
# HANDOFF_IFC_2026-08-11.md's proven harness build) -- not from a test run of this file.
#
# ---------------------------------------------------------------------------------------------
# Two supported configurations
# ---------------------------------------------------------------------------------------------
#
# (A) SUBDIRECTORY mode -- this directory is add_subdirectory()'d from inside a checkout of
#     https://github.com/ifcquery/IfcPlusPlus, as a sibling of examples/LoadFileExample, e.g. by
#     adding one line to the repo's own top-level CMakeLists.txt:
#         ADD_SUBDIRECTORY (path/to/this/ifcbridge)
#     In this mode the `IfcPlusPlus` target already exists (defined by
#     IfcPlusPlus/CMakeLists.txt, add_library(IfcPlusPlus STATIC ...)) and CMake's own
#     ${IFCPP_SOURCE_DIR} variable (from that repo's `project(IFCPP)` call) already points at the
#     checkout root. This is the configuration I could most easily reason about because it mirrors
#     examples/LoadFileExample/CMakeLists.txt line for line.
#
# (B) STANDALONE mode -- this directory is configured on its own, pointing at an IfcPlusPlus
#     checkout that has ALREADY been built (its .lib produced) via two variables:
#         -DMOBIUS_IFCPLUSPLUS_SOURCE_DIR=<path to the IfcPlusPlus checkout root>
#         -DMOBIUS_IFCPLUSPLUS_LIB_DIR=<directory containing IfcPlusPlus.lib / IfcPlusPlusd.lib>
#     IMPORTANT, and worth reading before assuming this "just works" like a normal installed
#     dependency: IfcPlusPlus's own `install(TARGETS IfcPlusPlus ...)` / `install(EXPORT ...)`
#     does NOT produce something this mode can consume via find_package() -- the root
#     CMakeLists.txt's CONFIGURE_PACKAGE_CONFIG_FILE block that would generate an
#     IFCPPConfig.cmake is commented out, so only a bare *-targets.cmake exists with no matching
#     Config.cmake wrapper, which find_package() requires. Worse, IfcPlusPlus's own
#     `install(DIRECTORY src/ifcpp DESTINATION include FILES_MATCHING PATTERN "*.h")` only
#     captures ifcpp/**/*.h -- it does NOT install src/external/Carve or src/external/glm at all,
#     and Carve's own headers are *.hpp, which that glob would miss even if the directory were
#     included. This DLL's implementation needs carve::mesh::Face/Edge/MeshSet and
#     carve::math::Matrix directly, so a "point at `cmake --install`'s output" mode is not
#     actually usable as IfcPlusPlus ships today. Standalone mode therefore points at the SOURCE
#     tree for headers (mirroring subdirectory mode's own include list exactly) and only uses
#     MOBIUS_IFCPLUSPLUS_LIB_DIR to locate the already-built library file. See NOTES.md.

cmake_minimum_required(VERSION 3.16)
project(MobiusIfcBridge CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
	message(WARNING "MobiusIfcBridge has only ever been reasoned about for MSVC/Win64 (Project "
		"Mobius is Win64-only, see MobiusIfcLibrary.Build.cs's own hard-fail on other platforms). "
		"Proceeding, but nothing here has been checked against any other toolchain.")
endif()

# -------------------------------------------------------------------------------------------
# Resolve where IfcPlusPlus's headers and library come from, and add the include directories.
#
# IfcPlusPlus/CMakeLists.txt declares every one of its own include directories PRIVATE
# (TARGET_INCLUDE_DIRECTORIES(IfcPlusPlus PRIVATE ...)), so linking against the `IfcPlusPlus`
# target -- even in subdirectory mode, where the target genuinely exists -- does NOT
# transitively propagate those paths to us the way a normal PUBLIC/INTERFACE dependency would.
# examples/LoadFileExample/CMakeLists.txt proves this is a real constraint, not a guess: it
# re-declares the IDENTICAL list of include directories itself rather than relying on the
# IfcPlusPlus target for them. We do the same thing here, verbatim, for both of our modes.
# -------------------------------------------------------------------------------------------

if(TARGET IfcPlusPlus)
	# Subdirectory mode. ${IFCPP_SOURCE_DIR} comes from IfcPlusPlus's own root CMakeLists.txt's
	# `project(IFCPP)` call and is already in scope by the time a sibling add_subdirectory() runs.
	set(_mobius_ifcpp_source_dir "${IFCPP_SOURCE_DIR}")
	message(STATUS "MobiusIfcBridge: subdirectory mode, IfcPlusPlus target found, source dir = ${_mobius_ifcpp_source_dir}")
else()
	# Standalone mode.
	if(NOT DEFINED MOBIUS_IFCPLUSPLUS_SOURCE_DIR)
		message(FATAL_ERROR
			"MobiusIfcBridge: no `IfcPlusPlus` target in scope (not add_subdirectory()'d from "
			"inside an IfcPlusPlus checkout), and MOBIUS_IFCPLUSPLUS_SOURCE_DIR was not set. "
			"Pass -DMOBIUS_IFCPLUSPLUS_SOURCE_DIR=<path to the IfcPlusPlus checkout root "
			"(the directory containing its top-level CMakeLists.txt and the IfcPlusPlus/ "
			"subfolder)>.")
	endif()
	if(NOT DEFINED MOBIUS_IFCPLUSPLUS_LIB_DIR)
		message(FATAL_ERROR
			"MobiusIfcBridge: standalone mode also requires -DMOBIUS_IFCPLUSPLUS_LIB_DIR=<dir "
			"containing the already-built IfcPlusPlus.lib / IfcPlusPlusd.lib> -- see this file's "
			"header comment for why `cmake --install`'s own output cannot be used here.")
	endif()
	set(_mobius_ifcpp_source_dir "${MOBIUS_IFCPLUSPLUS_SOURCE_DIR}")
	message(STATUS "MobiusIfcBridge: standalone mode, source dir = ${_mobius_ifcpp_source_dir}, lib dir = ${MOBIUS_IFCPLUSPLUS_LIB_DIR}")
endif()

# Identical, in order, to IfcPlusPlus/CMakeLists.txt's own TARGET_INCLUDE_DIRECTORIES(IfcPlusPlus
# PRIVATE ...) list and to examples/LoadFileExample/CMakeLists.txt's copy of the same list.
# Do not add MobiusIfcBridge's own directory to this list -- MobiusIfcBridge.h must stay
# reachable without any of these paths, since UE consumers only ever get install/include.
set(_mobius_ifcpp_include_dirs
	"${_mobius_ifcpp_source_dir}/IfcPlusPlus/src"
	"${_mobius_ifcpp_source_dir}/IfcPlusPlus/src/ifcpp/IFC4X3/include"
	"${_mobius_ifcpp_source_dir}/IfcPlusPlus/src/external"
	"${_mobius_ifcpp_source_dir}/IfcPlusPlus/src/external/glm"
	"${_mobius_ifcpp_source_dir}/IfcPlusPlus/src/external/Carve/include"
	"${_mobius_ifcpp_source_dir}/IfcPlusPlus/src/external/Carve/src"
	"${_mobius_ifcpp_source_dir}/IfcPlusPlus/src/external/Carve/src/include"
	"${_mobius_ifcpp_source_dir}/IfcPlusPlus/src/external/Carve/src/common"
	"${_mobius_ifcpp_source_dir}/IfcPlusPlus/src/external/Carve/build/src"
)

# -------------------------------------------------------------------------------------------
# The DLL target.
# -------------------------------------------------------------------------------------------

add_library(MobiusIfcBridge SHARED
	MobiusIfcBridge.h
	MobiusIfcBridge.cpp
)

set_target_properties(MobiusIfcBridge PROPERTIES
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON
	# Deliberately NOT setting a DEBUG_POSTFIX the way IfcPlusPlus sets "d" for itself.
	# MobiusIfcLibrary.Build.cs (staged alongside this file) hardcodes the single filenames
	# MobiusIfcBridge.lib / MobiusIfcBridge.dll with no per-config variant, so this target must
	# produce exactly those names in every configuration.
	DEBUG_POSTFIX ""
)

target_include_directories(MobiusIfcBridge PRIVATE ${_mobius_ifcpp_include_dirs})

target_compile_definitions(MobiusIfcBridge PRIVATE
	MOBIUSIFC_BUILD_DLL     # flips MobiusIfcBridge.h's MOBIUSIFC_API to __declspec(dllexport) for
	                        # this target only -- never propagate this to a consumer, see the
	                        # header's own comment and MobiusIfcLibrary.Build.cs's matching one.
	IFCQUERY_STATIC_LIB     # REQUIRED, not optional: ifcpp/model/GlobalDefines.h's IFCQUERY_EXPORT
	                        # macro expands to __declspec(dllimport) unless either IFCQUERY_LIB
	                        # (building IfcPlusPlus itself) or IFCQUERY_STATIC_LIB is defined.
	                        # IfcPlusPlus/CMakeLists.txt builds IfcPlusPlus as STATIC and defines
	                        # IFCQUERY_STATIC_LIB for itself; every consumer of its headers -- this
	                        # target included -- must define the same macro or every
	                        # IFCQUERY_EXPORT-annotated class (BuildingModel, ReaderSTEP, ...)
	                        # resolves to a dllimport that does not match the static link, which is
	                        # a linker error. examples/LoadFileExample/CMakeLists.txt confirms this
	                        # is required, not a guess -- it defines the same macro for itself.
	UNICODE
	_UNICODE                # Matches IfcPlusPlus's and LoadFileExample's own compile definitions;
	                        # not verified to be strictly necessary for the subset of IFC++ this
	                        # shim calls, but matching the proven-working example removes an axis
	                        # of divergence for zero cost.
)

if(MSVC)
	target_compile_options(MobiusIfcBridge PRIVATE /MP)
endif()

if(TARGET IfcPlusPlus)
	# Target-based link: CMake resolves the correct per-configuration file (including
	# IfcPlusPlus's own DEBUG_POSTFIX "d") automatically because we are linking the TARGET, not a
	# bare library name -- unlike examples/LoadFileExample's older `optimized IfcPlusPlus debug
	# IfcPlusPlusd` form, which is only necessary when there is no live target to link against.
	target_link_libraries(MobiusIfcBridge PRIVATE IfcPlusPlus)
else()
	# Standalone mode: no live target, so resolve the actual library file by hand. IfcPlusPlus's
	# own DEBUG_POSTFIX is "d" (IfcPlusPlusd.lib) -- honour that per-config the same way the
	# example's `optimized/debug` link line does, since there is no target to do it for us.
	find_library(_mobius_ifcpp_lib_release NAMES IfcPlusPlus PATHS "${MOBIUS_IFCPLUSPLUS_LIB_DIR}" NO_DEFAULT_PATH)
	find_library(_mobius_ifcpp_lib_debug   NAMES IfcPlusPlusd PATHS "${MOBIUS_IFCPLUSPLUS_LIB_DIR}" NO_DEFAULT_PATH)

	if(NOT _mobius_ifcpp_lib_release AND NOT _mobius_ifcpp_lib_debug)
		message(FATAL_ERROR
			"MobiusIfcBridge: found neither IfcPlusPlus.lib nor IfcPlusPlusd.lib under "
			"${MOBIUS_IFCPLUSPLUS_LIB_DIR}. Build IfcPlusPlus first (see "
			"HANDOFF_IFC_2026-08-11.md section 4 for the exact CMake invocation).")
	endif()

	if(_mobius_ifcpp_lib_release)
		target_link_libraries(MobiusIfcBridge PRIVATE optimized "${_mobius_ifcpp_lib_release}")
	else()
		message(STATUS "MobiusIfcBridge: only a Debug IfcPlusPlusd.lib was found; Release builds of this target will fail to link until a Release IfcPlusPlus.lib exists.")
	endif()
	if(_mobius_ifcpp_lib_debug)
		target_link_libraries(MobiusIfcBridge PRIVATE debug "${_mobius_ifcpp_lib_debug}")
	else()
		message(STATUS "MobiusIfcBridge: only a Release IfcPlusPlus.lib was found; Debug builds of this target will fail to link until a Debug IfcPlusPlusd.lib exists.")
	endif()
endif()

# -------------------------------------------------------------------------------------------
# Install layout, matching MobiusIfcLibrary.Build.cs's expectations exactly.
# -------------------------------------------------------------------------------------------

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	# Default to installing next to this CMakeLists.txt (install/{include,lib,bin}) unless the
	# caller explicitly overrides CMAKE_INSTALL_PREFIX -- this matches where
	# MobiusIfcLibrary.Build.cs looks (Path.Combine(ModuleDirectory, "install")) once this
	# directory is relocated to its final plugin path.
	set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/install" CACHE PATH "" FORCE)
endif()

install(FILES MobiusIfcBridge.h DESTINATION include)

install(TARGETS MobiusIfcBridge
	RUNTIME DESTINATION bin      # MobiusIfcBridge.dll
	LIBRARY DESTINATION bin      # (non-Windows shared-object convention; unused on Win64)
	ARCHIVE DESTINATION lib      # MobiusIfcBridge.lib (import lib)
)

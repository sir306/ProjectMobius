# Replace every occurrence of FROM with TO in FILE, in place.
#   cmake -DFILE=<path> -DFROM=<text> -DTO=<text> -P ScrubBuildPath.cmake
#
# Used on generated sources that embed the build machine's paths as string literals (HDF5's
# H5build_settings.c), so a statically linked dependency does not ship the checkout location.
# The file is rewritten only when something changes, so an unchanged tree does not rebuild.
if(NOT EXISTS "${FILE}")
  message(STATUS "ScrubBuildPath: ${FILE} not found; nothing to scrub")
  return()
endif()

file(READ "${FILE}" _original)
string(REPLACE "${FROM}" "${TO}" _scrubbed "${_original}")
if(NOT _scrubbed STREQUAL _original)
  file(WRITE "${FILE}" "${_scrubbed}")
  message(STATUS "ScrubBuildPath: removed '${FROM}' from ${FILE}")
endif()

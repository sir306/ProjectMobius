# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "CMakeFiles\\appPlotUE_Data_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\appPlotUE_Data_autogen.dir\\ParseCache.txt"
  "appPlotUE_Data_autogen"
  )
endif()

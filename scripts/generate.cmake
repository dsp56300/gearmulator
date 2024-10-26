include(${CMAKE_CURRENT_LIST_DIR}/synths.cmake)

# these need to be specified explicitly

validateToggle(gearmulator_BUILD_JUCEPLUGIN)
validateToggle(gearmulator_BUILD_FX_PLUGIN)

if(NOT DEFINED gearmulator_SOURCE_DIR)
	message(FATAL_ERROR "gearmulator_SOURCE_DIR needs to be specified")
endif()

if(NOT DEFINED gearmulator_BINARY_DIR)
	message(FATAL_ERROR "gearmulator_BINARY_DIR needs to be specified")
endif()

# build Release by default if not specified otherwise
if(NOT DEFINED CMAKE_BUILD_TYPE)
	message(STATUS "CMAKE_BUILD_TYPE unspecified, setting to Release")
	set(CMAKE_BUILD_TYPE Release)
endif()

set(args ${gearmulator_SOURCE_DIR})

if(APPLE)
	# We need Xcode for macOS builds to prevent that the VST3 bundles Info.plist is missing information
	set(args ${args} -G Xcode)
endif()

set(args ${args} -B ${gearmulator_BINARY_DIR})

set(args ${args} -Dgearmulator_BUILD_JUCEPLUGIN=${gearmulator_BUILD_JUCEPLUGIN})
set(args ${args} -Dgearmulator_BUILD_FX_PLUGIN=${gearmulator_BUILD_FX_PLUGIN})
set(args ${args} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})

set(args ${args} -Dgearmulator_BUILD_JUCEPLUGIN_VST2=ON)
set(args ${args} -Dgearmulator_BUILD_JUCEPLUGIN_VST3=ON)
set(args ${args} -Dgearmulator_BUILD_JUCEPLUGIN_CLAP=ON)
set(args ${args} -Dgearmulator_BUILD_JUCEPLUGIN_LV2=ON)
set(args ${args} -Dgearmulator_BUILD_JUCEPLUGIN_AU=ON)

foreach(S IN LISTS synths)
	set(args ${args} -D${S}=${${S}})
endforeach()

execute_process(COMMAND cmake ${args} COMMAND_ECHO STDOUT WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR} COMMAND_ERROR_IS_FATAL ANY)

include(${CMAKE_CURRENT_LIST_DIR}/products.cmake)

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

# Visual Studio platform, e.g. ARM64 - which an x64 machine builds too, as a cross build
if(CMAKE_GENERATOR_PLATFORM)
	set(args ${args} -A ${CMAKE_GENERATOR_PLATFORM})
endif()

set(args ${args} -Dgearmulator_BUILD_JUCEPLUGIN=${gearmulator_BUILD_JUCEPLUGIN})
set(args ${args} -Dgearmulator_BUILD_FX_PLUGIN=${gearmulator_BUILD_FX_PLUGIN})
set(args ${args} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})

# Build all plugin formats unless explicitly disabled.
foreach(F VST2 VST3 CLAP LV2 AU)
	if(NOT DEFINED gearmulator_BUILD_JUCEPLUGIN_${F})
		set(gearmulator_BUILD_JUCEPLUGIN_${F} ON)
	endif()
	set(args ${args} -Dgearmulator_BUILD_JUCEPLUGIN_${F}=${gearmulator_BUILD_JUCEPLUGIN_${F}})
endforeach()

# Forward the opt-in standalone build flag.
if(NOT DEFINED gearmulator_BUILD_JUCEPLUGIN_Standalone)
	set(gearmulator_BUILD_JUCEPLUGIN_Standalone off)
endif()
set(args ${args} -Dgearmulator_BUILD_JUCEPLUGIN_Standalone=${gearmulator_BUILD_JUCEPLUGIN_Standalone})

# An unspecified SDK path leaves automatic ASIO discovery enabled.
if(DEFINED gearmulator_ASIO_SDK_PATH)
	set(args ${args} -Dgearmulator_ASIO_SDK_PATH=${gearmulator_ASIO_SDK_PATH})
endif()

foreach(S IN LISTS products)
	set(args ${args} -D${S}=${${S}})
endforeach()

execute_process(COMMAND cmake ${args} COMMAND_ECHO STDOUT WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR} COMMAND_ERROR_IS_FATAL ANY)

# Debug symbols from earlier builds, before this one writes its own. A CI workspace is shared by every
# branch and symbols are the one output nothing clears, so deploySymbols would otherwise archive and
# upload every product ever built there - 5.6 GB of PDBs, 10.5 GB of dSYMs, for a build that made one
# 7 MB product. It belongs here rather than in a job definition: every runner configures through this
# script, so none of them has to remember to do it.
execute_process(COMMAND ${CMAKE_COMMAND}
	-Dgearmulator_BINARY_DIR=${gearmulator_BINARY_DIR}
	-Dgearmulator_SOURCE_DIR=${gearmulator_SOURCE_DIR}
	-DCLEAN=1 -P ${CMAKE_CURRENT_LIST_DIR}/deploySymbols.cmake
	COMMAND_ECHO STDOUT WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR} COMMAND_ERROR_IS_FATAL ANY)

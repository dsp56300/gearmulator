set(synths 
	gearmulator_SYNTH_OSIRUS
	gearmulator_SYNTH_OSTIRUS
	gearmulator_SYNTH_VAVRA
	gearmulator_SYNTH_XENIA
)

macro(validateToggle NAME)
	if(NOT DEFINED ${NAME} OR (NOT ${${NAME}} STREQUAL "on" AND NOT ${${NAME}} STREQUAL "off"))
		message(FATAL_ERROR "Variable " ${NAME} " needs to be set to on or off but got '" ${${NAME}} "'")
	else()
		message(STATUS ${NAME}=${${NAME}})
	endif()
endmacro()

# turn off all synths that are not explicitly specified
foreach(S IN LISTS synths)
	if(NOT DEFINED ${S})
		set(${S} off)
		message(STATUS "Synth ${S} unspecified, turning off")
	else()
		validateToggle(${S})
	endif()
endforeach()

# these need to be specified explicitly
validateToggle(gearmulator_BUILD_JUCEPLUGIN)
validateToggle(gearmulator_BUILD_FX_PLUGIN)

if(NOT DEFINED gearmulator_SOURCE_DIR)
	message(FATAL_ERROR "gearmulator_SOURCE_DIR needs to be specified")
endif()

if(NOT DEFINED gearmulator_BINARY_DIR)
	message(FATAL_ERROR "gearmulator_BINARY_DIR needs to be specified")
endif()

# build Release by default
if(NOT DEFINED CMAKE_BUILD_TYPE)
	message(STATUS "CMAKE_BUILD_TYPE unspecified, setting to Release")
	set(CMAKE_BUILD_TYPE Release)
endif()

set(args ${gearmulator_SOURCE_DIR})

if(APPLE)
	set(args ${args} -G Xcode)
endif()

set(args ${args} -B ${gearmulator_BINARY_DIR})

set(args ${args} -Dgearmulator_BUILD_JUCEPLUGIN=${gearmulator_BUILD_JUCEPLUGIN})
set(args ${args} -Dgearmulator_BUILD_FX_PLUGIN=${gearmulator_BUILD_FX_PLUGIN})
set(args ${args} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})

foreach(S IN LISTS synths)
	set(args ${args} -D${S}=${${S}})
endforeach()

message(STATUS "Executing command: cmake ${args}")

execute_process(COMMAND cmake ${args} COMMAND_ECHO STDOUT WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR} COMMAND_ERROR_IS_FATAL ANY)

set(synths 
	gearmulator_SYNTH_OSIRUS
	gearmulator_SYNTH_OSTIRUS
	gearmulator_SYNTH_VAVRA
	gearmulator_SYNTH_XENIA
	gearmulator_SYNTH_NODALRED2X
)

set(gearmulator_SYNTH_OSIRUS_name Osirus)
set(gearmulator_SYNTH_OSTIRUS_name OsTIrus)
set(gearmulator_SYNTH_VAVRA_name Vavra)
set(gearmulator_SYNTH_XENIA_name Xenia)
set(gearmulator_SYNTH_NODALRED2X_name NodalRed2x)

set(gearmulator_SYNTH_OSIRUS_folder osirus)
set(gearmulator_SYNTH_OSTIRUS_folder ostirus)
set(gearmulator_SYNTH_VAVRA_folder vavra)
set(gearmulator_SYNTH_XENIA_folder xenia)
set(gearmulator_SYNTH_NODALRED2X_folder nodalred2x)

macro(validateToggle NAME)
	if(NOT DEFINED ${NAME} OR (NOT ${${NAME}} STREQUAL "on" AND NOT ${${NAME}} STREQUAL "off"))
		message(FATAL_ERROR "Variable " ${NAME} " needs to be set to on or off but got '" ${${NAME}} "'")
	else()
		message(STATUS ${NAME}=${${NAME}})
	endif()
endmacro()

# turn off all synths that are not explicitly specified
foreach(S IN LISTS synths)
	if(NOT DEFINED ${S}_name)
		message(FATAL_ERROR "No name defined for synth ${S}")
	endif()
	if(NOT DEFINED ${S}_folder)
		message(FATAL_ERROR "No folder defined for synth ${S}")
	endif()
	if(NOT DEFINED ${S})
		set(${S} off)
		message(STATUS "Synth ${S} unspecified, turning off")
	else()
		validateToggle(${S})
	endif()
endforeach()

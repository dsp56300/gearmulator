set(products 
	gearmulator_SYNTH_OSIRUS
	gearmulator_SYNTH_OSTIRUS
	gearmulator_SYNTH_VAVRA
	gearmulator_SYNTH_XENIA
	gearmulator_SYNTH_NODALRED2X
	gearmulator_SYNTH_JE8086
	gearmulator_COMPONENT_DSPBRIDGE
)

macro(validateToggle NAME)
	if(NOT DEFINED ${NAME} OR (NOT ${${NAME}} STREQUAL "on" AND NOT ${${NAME}} STREQUAL "off"))
		message(FATAL_ERROR "Variable " ${NAME} " needs to be set to on or off but got '" ${${NAME}} "'")
	else()
		message(STATUS ${NAME}=${${NAME}})
	endif()
endmacro()

# turn off all products that are not explicitly specified
foreach(S IN LISTS products)
	if(NOT DEFINED ${S})
		set(${S} off)
		message(STATUS "Product ${S} unspecified, turning off")
	else()
		validateToggle(${S})
	endif()
endforeach()

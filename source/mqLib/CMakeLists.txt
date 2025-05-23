cmake_minimum_required(VERSION 3.10)
project(mqLib)

include(bin2h.cmake)

add_library(mqLib STATIC)

set(EMBED_ROM OFF)
set(ROM "${CMAKE_CURRENT_SOURCE_DIR}/microQ223.swapped.BIN")

if(EXISTS ${ROM})
	set(EMBED_ROM ON)
	if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/romData.h)
		bin2h(SOURCE_FILE ${ROM} HEADER_FILE "romData.h" VARIABLE_NAME "ROM_DATA")
	endif()
	target_compile_definitions(mqLib PRIVATE EMBED_ROM=1)
else()
	target_compile_definitions(mqLib PRIVATE EMBED_ROM=0)
endif()

set(SOURCES
	buttons.cpp buttons.h
	device.cpp device.h
	lcd.cpp lcd.h
	leds.cpp leds.h
	rom.cpp rom.h
	romloader.cpp romloader.h
	microq.cpp microq.h
	mqbuildconfig.h
	mqdsp.cpp mqdsp.h
	mqhardware.cpp mqhardware.h
	mqmc.cpp mqmc.h
	mqstate.cpp mqstate.h
	mqmiditypes.h
	mqsysexremotecontrol.cpp mqsysexremotecontrol.h
	mqtypes.h
)

if(${EMBED_ROM})
	list(APPEND SOURCES romData.h)
endif()

target_sources(mqLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(mqLib PUBLIC wLib)

if(DSP56300_DEBUGGER)
	target_link_libraries(mqLib PUBLIC dsp56kDebugger)
endif()
set_property(TARGET mqLib PROPERTY FOLDER "Vavra")

target_include_directories(mqLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

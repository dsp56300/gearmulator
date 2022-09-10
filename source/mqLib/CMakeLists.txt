cmake_minimum_required(VERSION 3.10)
project(mqLib)

include(bin2h.cmake)

#bin2h(SOURCE_FILE "microQ223.swapped.BIN" HEADER_FILE "romData.h" VARIABLE_NAME "ROM_DATA")

add_library(mqLib STATIC)

set(SOURCES
	buttons.cpp buttons.h
	dspBootCode.h
	lcd.cpp lcd.h
	leds.cpp leds.h
	rom.cpp rom.h
	romData.h
	microq.cpp microq.h
	mqdsp.cpp mqdsp.h
	mqhardware.cpp mqhardware.h
	mqmc.cpp mqmc.h
	mqtypes.h
)

target_sources(mqLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(mqLib PUBLIC synthLib 68kEmu)

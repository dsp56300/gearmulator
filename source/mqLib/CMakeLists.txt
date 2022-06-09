cmake_minimum_required(VERSION 3.10)
project(mqLib)

add_library(mqLib STATIC)

set(SOURCES
	buttons.cpp buttons.h
	dspBootCode.h
	lcd.cpp lcd.h
	rom.cpp rom.h
	mqdsp.cpp mqdsp.h
	mqhardware.cpp mqhardware.h
	mqmc.cpp mqmc.h
)

target_sources(mqLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(mqLib PUBLIC synthLib 68kEmu)

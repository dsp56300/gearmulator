cmake_minimum_required(VERSION 3.10)
project(virusLib)

add_library(virusLib STATIC)

set(SOURCES
	demoplayback.cpp demoplayback.h
	device.cpp device.h
	midiOutParser.cpp midiOutParser.h
	romfile.cpp romfile.h
	microcontroller.cpp microcontroller.h
	microcontrollerTypes.cpp microcontrollerTypes.h
)

target_sources(virusLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(virusLib PUBLIC synthLib)

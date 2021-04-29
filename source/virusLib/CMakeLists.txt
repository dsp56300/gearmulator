cmake_minimum_required(VERSION 3.10)
project(ceLib)

add_library(virusLib STATIC)

set(SOURCES
	device.cpp device.h
	midiTypes.h
	plugin.cpp plugin.h
	romfile.cpp romfile.h
	syx.cpp syx.h
	midi.cpp midi.h
)

target_sources(virusLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_include_directories(virusLib PUBLIC ../portmidi/pm_common/)
target_link_libraries(virusLib PUBLIC dsp56kEmu portmidi-static)

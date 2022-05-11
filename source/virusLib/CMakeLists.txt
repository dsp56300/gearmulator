cmake_minimum_required(VERSION 3.10)
project(virusLib)

add_library(virusLib STATIC)

option(VIRUS_SUPPORT_TI "Support Virus TI family" off)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/buildconfig.h.in ${CMAKE_CURRENT_SOURCE_DIR}/buildconfig.h)

set(SOURCES
	buildconfig.h
	buildconfig.h.in
	demopacketvalidator.cpp demopacketvalidator.h
	demoplayback.cpp demoplayback.h
	demoplaybackTI.cpp demoplaybackTI.h
	device.cpp device.h
	dspSingle.cpp dspSingle.h
	dspSingleSnow.cpp dspSingleSnow.h
	dspMultiTI.cpp dspMultiTI.h
	midiOutParser.cpp midiOutParser.h
	unpacker.cpp unpacker.h
	romfile.cpp romfile.h
	microcontroller.cpp microcontroller.h
	microcontrollerTypes.cpp microcontrollerTypes.h
	utils.h
)

target_sources(virusLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(virusLib PUBLIC synthLib)

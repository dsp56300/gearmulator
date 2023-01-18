cmake_minimum_required(VERSION 3.10)
project(virusLib)

add_library(virusLib STATIC)

if(${CMAKE_PROJECT_NAME}_SYNTH_OSTIRUS)
	set(VIRUS_SUPPORT_TI on)
else
	set(VIRUS_SUPPORT_TI off)
endif()

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
	hdi08List.cpp hdi08List.h
	hdi08MidiQueue.cpp hdi08MidiQueue.h
	hdi08TxParser.cpp hdi08TxParser.h
	hdi08Queue.cpp hdi08Queue.h
	romfile.cpp romfile.h
	microcontroller.cpp microcontroller.h
	microcontrollerTypes.cpp microcontrollerTypes.h
	unpacker.cpp unpacker.h
	utils.h
)

target_sources(virusLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(virusLib PUBLIC synthLib)

if(DSP56300_DEBUGGER)
	target_link_libraries(virusLib PUBLIC dsp56kDebugger)
endif()

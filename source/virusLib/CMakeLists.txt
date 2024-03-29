cmake_minimum_required(VERSION 3.10)
project(virusLib)

add_library(virusLib STATIC)

set(SOURCES
	demoplayback.cpp demoplayback.h
	device.cpp device.h
	dspSingle.cpp dspSingle.h
	hdi08List.cpp hdi08List.h
	hdi08MidiQueue.cpp hdi08MidiQueue.h
	hdi08TxParser.cpp hdi08TxParser.h
	hdi08Queue.cpp hdi08Queue.h
	romfile.cpp romfile.h
	microcontroller.cpp microcontroller.h
	microcontrollerTypes.cpp microcontrollerTypes.h
	midiFileToRomData.cpp midiFileToRomData.h
	utils.h
)

target_sources(virusLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(virusLib PUBLIC synthLib)

if(DSP56300_DEBUGGER)
	target_link_libraries(virusLib PUBLIC dsp56kDebugger)
endif()

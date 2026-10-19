cmake_minimum_required(VERSION 3.10)
project(virusLib)

add_library(virusLib STATIC)

set(SOURCES
	arpeggiator.cpp arpeggiator.h
	demoplayback.cpp demoplayback.h
	demoplaybackTI.cpp demoplaybackTI.h
	device.cpp device.h
	deviceModel.cpp deviceModel.h
	dspMemoryPatch.cpp dspMemoryPatch.h
	dspMemoryPatches.cpp dspMemoryPatches.h
	dspSingle.cpp dspSingle.h
	dspSingleSnow.cpp dspSingleSnow.h
	dspMultiTI.cpp dspMultiTI.h
	frontpanelState.cpp frontpanelState.h
	hdi08List.cpp hdi08List.h
	hdi08MidiQueue.cpp hdi08MidiQueue.h
	hdi08TxParser.cpp hdi08TxParser.h
	hdi08Queue.cpp hdi08Queue.h
	romfile.cpp romfile.h
	romloader.cpp romloader.h
	microcontroller.cpp microcontroller.h
	microcontrollerTypes.cpp microcontrollerTypes.h
	midiFileToRomData.cpp midiFileToRomData.h
	unpacker.cpp unpacker.h
	utils.h
)

target_sources(virusLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(virusLib PUBLIC synthLib dsp56kEmu)

if(DSP56300_DEBUGGER)
	target_link_libraries(virusLib PUBLIC dsp56kDebugger)
endif()

set_property(TARGET virusLib PROPERTY FOLDER "Virus")

if(BUILD_TESTING)
	add_executable(virusArpeggiator_test arpeggiator_test.cpp)
	target_link_libraries(virusArpeggiator_test PRIVATE virusLib)
	# This directory is pulled in with EXCLUDE_FROM_ALL, which would keep the test from being built
	set_property(TARGET virusArpeggiator_test PROPERTY EXCLUDE_FROM_ALL FALSE)
	set_property(TARGET virusArpeggiator_test PROPERTY FOLDER "Tests")
	add_test(NAME virusArpeggiator COMMAND virusArpeggiator_test)
endif()

target_include_directories(virusLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

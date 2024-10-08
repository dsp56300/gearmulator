cmake_minimum_required(VERSION 3.10)
project(synthLib)

set(SYNTHLIB_DEMO_MODE OFF CACHE BOOL "Demo Mode" FORCE)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/buildconfig.h.in ${CMAKE_CURRENT_SOURCE_DIR}/buildconfig.h)

add_library(synthLib STATIC)

set(SOURCES
	audiobuffer.cpp audiobuffer.h
	audioTypes.h
	buildconfig.h buildconfig.h.in
	device.cpp device.h
	deviceException.cpp deviceException.h
	deviceTypes.h
	dspMemoryPatch.cpp dspMemoryPatch.h
	lv2PresetExport.cpp lv2PresetExport.h
	midiBufferParser.cpp midiBufferParser.h
	midiClock.cpp midiClock.h
	midiToSysex.cpp midiToSysex.h
	midiTypes.h
	os.cpp os.h
	plugin.cpp plugin.h
	resampler.cpp resampler.h
	resamplerInOut.cpp resamplerInOut.h
	romLoader.cpp romLoader.h
	sysexToMidi.cpp sysexToMidi.h
	wavReader.cpp wavReader.h
	wavTypes.h
	wavWriter.cpp wavWriter.h
)

target_sources(synthLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(synthLib PUBLIC resample dsp56kEmu baseLib)

if(NOT MSVC)
	target_link_libraries(synthLib PUBLIC dl)
endif()
set_property(TARGET synthLib PROPERTY FOLDER "Gearmulator")

target_include_directories(synthLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

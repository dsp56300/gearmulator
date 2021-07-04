cmake_minimum_required(VERSION 3.10)
project(ceLib)

add_library(virusLib STATIC)

set(SOURCES
	audiobuffer.cpp audiobuffer.h
	device.cpp device.h
	deviceStandalone.cpp deviceStandalone.h
	midiTypes.h
	midi.cpp midi.h
	midiOutParser.cpp midiOutParser.h
	plugin.cpp plugin.h
	resampler.cpp resampler.h
	resamplerInOut.cpp resamplerInOut.h
	romfile.cpp romfile.h
	syx.cpp syx.h
	wavWriter.cpp wavWriter.h
)

target_sources(virusLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_include_directories(virusLib PUBLIC ../portmidi/pm_common/ ../portaudio/include)
target_link_libraries(virusLib PUBLIC dsp56kEmu portmidi-static resample)

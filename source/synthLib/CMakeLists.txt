cmake_minimum_required(VERSION 3.10)
project(synthLib)

add_library(synthLib STATIC)

set(SOURCES
	audiobuffer.cpp audiobuffer.h
	device.cpp device.h
	midiTypes.h
	midi.cpp midi.h
	os.cpp os.h
	plugin.cpp plugin.h
	resampler.cpp resampler.h
	resamplerInOut.cpp resamplerInOut.h
	wavWriter.cpp wavWriter.h
)

target_sources(synthLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_include_directories(synthLib PUBLIC ../portmidi/pm_common/ ../portaudio/include)
target_link_libraries(synthLib PUBLIC portmidi-static resample)

if(NOT MSVC)
	target_link_libraries(synthLib PUBLIC dl)
endif()

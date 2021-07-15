cmake_minimum_required(VERSION 3.10)
project(synthLib)

add_library(synthLib STATIC)

set(SOURCES
	audiobuffer.cpp audiobuffer.h
	device.cpp device.h
	deviceTypes.h
	midiTypes.h
	os.cpp os.h
	plugin.cpp plugin.h
	resampler.cpp resampler.h
	resamplerInOut.cpp resamplerInOut.h
	wavWriter.cpp wavWriter.h
)

target_sources(synthLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(synthLib PUBLIC resample dsp56kEmu)

if(NOT MSVC)
	target_link_libraries(synthLib PUBLIC dl)
endif()

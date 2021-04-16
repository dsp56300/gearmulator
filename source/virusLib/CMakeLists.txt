cmake_minimum_required(VERSION 3.10)
project(ceLib)

add_library(virusLib STATIC)

set(SOURCES
	accessVirus.cpp accessVirus.h syx.h syx.cpp
)

target_sources(virusLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(virusLib PUBLIC dsp56kEmu)

cmake_minimum_required(VERSION 3.10)

project(virusIntegrationTest)

add_executable(virusIntegrationTest)

set(SOURCES
	integrationTest.cpp integrationTest.h
	../dsp56300/source/disassemble/commandline.cpp
	../dsp56300/source/disassemble/commandline.h
)

target_sources(virusIntegrationTest PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(virusIntegrationTest PUBLIC virusConsoleLib)

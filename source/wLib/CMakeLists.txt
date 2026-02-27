cmake_minimum_required(VERSION 3.10)

project(wLib)

add_library(wLib STATIC)

set(SOURCES
	wDevice.cpp wDevice.h
	wDsp.cpp wDsp.h
	wHardware.cpp wHardware.h
	wMidiTypes.h
	wSysexRemoteControl.cpp wSysexRemoteControl.h
	wPlugin.cpp wPlugin.h
	wRom.cpp wRom.h
	wState.cpp wState.h
)

target_sources(wLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(wLib PUBLIC hardwareLib synthLib dsp56kEmu)

target_include_directories(wLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

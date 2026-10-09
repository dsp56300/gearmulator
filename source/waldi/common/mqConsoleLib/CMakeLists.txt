cmake_minimum_required(VERSION 3.10)
project(mqConsoleLib)

add_library(mqConsoleLib STATIC)

set(SOURCES
	audioOutput.h
	audioOutput.cpp
	audioOutputPA.cpp audioOutputPA.h
	audioOutputWAV.cpp audioOutputWAV.h
	device.cpp device.h
	midiDevice.cpp midiDevice.h
	midiInput.cpp midiInput.h
	midiOutput.cpp midiOutput.h
	mqGui.cpp mqGui.h
	mqGuiBase.cpp mqGuiBase.h
	mqKeyInput.cpp mqKeyInput.h
	mqSettingsGui.cpp mqSettingsGui.h
)

target_sources(mqConsoleLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(mqConsoleLib PUBLIC mqLib cpp-terminal)
set_property(TARGET mqConsoleLib PROPERTY FOLDER "Vavra")

target_include_directories(mqConsoleLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

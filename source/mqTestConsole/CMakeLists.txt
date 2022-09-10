cmake_minimum_required(VERSION 3.10)

project(mqTestConsole)

add_executable(mqTestConsole)

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
	mqSettingsGui.cpp mqSettingsGui.h
	mqTestConsole.cpp
)

if(NOT ANDROID)
	list(APPEND SOURCES ../portmidi/pm_common/portmidi.h)
endif()

target_sources(mqTestConsole PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(mqTestConsole PUBLIC mqLib cpp-terminal portaudio_static)
target_compile_definitions(mqTestConsole PUBLIC "ANDROID")

if(NOT ANDROID)
	target_link_libraries(mqTestConsole PUBLIC portaudio_static)
endif()

if(UNIX AND NOT APPLE)
	target_link_libraries(mqTestConsole PUBLIC -static-libgcc -static-libstdc++)
endif()

install(TARGETS mqTestConsole DESTINATION . COMPONENT STANDALONE)

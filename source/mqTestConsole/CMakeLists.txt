cmake_minimum_required(VERSION 3.10)

project(mqTestConsole)

add_executable(mqTestConsole)

set(SOURCES
	audioOutput.h
	audioOutput.cpp
	audioOutputPA.cpp audioOutputPA.h
	audioOutputWAV.cpp audioOutputWAV.h
	mqGui.cpp mqGui.h
	mqTestConsole.cpp
)

target_sources(mqTestConsole PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(mqTestConsole PUBLIC mqLib cpp-terminal portaudio_static)

if(UNIX AND NOT APPLE)
	target_link_libraries(mqTestConsole PUBLIC -static-libgcc -static-libstdc++)
endif()

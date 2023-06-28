cmake_minimum_required(VERSION 3.10)

project(mqTestConsole)

add_executable(mqTestConsole)

set(SOURCES
	mqTestConsole.cpp
)

if(NOT ANDROID)
	list(APPEND SOURCES ../portmidi/pm_common/portmidi.h)
endif()

target_sources(mqTestConsole PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(mqTestConsole PUBLIC mqConsoleLib portaudio_static)
if(ANDROID)
	target_compile_definitions(mqTestConsole PUBLIC "ANDROID")
else()
	target_link_libraries(mqTestConsole PUBLIC portmidi-static)
endif()

if(UNIX AND NOT APPLE)
	target_link_libraries(mqTestConsole PUBLIC -static-libgcc -static-libstdc++)
endif()

#install(TARGETS mqTestConsole DESTINATION . COMPONENT STANDALONE)

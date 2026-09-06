cmake_minimum_required(VERSION 3.10)

project(mqTestConsole)

add_executable(mqTestConsole)

set(SOURCES
	mqTestConsole.cpp
)

target_sources(mqTestConsole PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(mqTestConsole PUBLIC mqConsoleLib portaudio_static)
if(ANDROID)
	target_compile_definitions(mqTestConsole PUBLIC "ANDROID")
else()
	target_link_libraries(mqTestConsole PUBLIC portmidi-static)
endif()

#install(TARGETS mqTestConsole DESTINATION . COMPONENT STANDALONE)
set_property(TARGET mqTestConsole PROPERTY FOLDER "Vavra")

cmake_minimum_required(VERSION 3.10)

project(commandlineGenerator)

add_executable(commandlineGenerator)

set(SOURCES
	commandlineGenerator.cpp
)

target_sources(commandlineGenerator PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(commandlineGenerator PUBLIC baseLib)

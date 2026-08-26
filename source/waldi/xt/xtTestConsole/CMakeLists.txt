cmake_minimum_required(VERSION 3.10)

project(xtTestConsole)

add_executable(xtTestConsole)

set(SOURCES
	xtTestConsole.cpp
)

target_sources(xtTestConsole PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(xtTestConsole PUBLIC xtLib)
set_property(TARGET xtTestConsole PROPERTY FOLDER "Xenia")

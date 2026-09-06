cmake_minimum_required(VERSION 3.10)

project(n2xTestConsole)

add_executable(n2xTestConsole)

set(SOURCES
	n2xTestConsole.cpp
)

target_sources(n2xTestConsole PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(n2xTestConsole PUBLIC n2xLib)
set_property(TARGET n2xTestConsole PROPERTY FOLDER "N2x")

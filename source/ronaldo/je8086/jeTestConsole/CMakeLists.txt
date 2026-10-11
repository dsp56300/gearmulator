cmake_minimum_required(VERSION 3.10)

project(JE8086TestConsole)

add_executable(JE8086TestConsole)

set(SOURCES
	jeTestConsole.cpp
)

target_sources(JE8086TestConsole PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(JE8086TestConsole PUBLIC jeLib)

tus_installConsole(JE8086TestConsole COMPONENT JE8086TestConsole)

set_property(TARGET JE8086TestConsole PROPERTY FOLDER "JE8086")

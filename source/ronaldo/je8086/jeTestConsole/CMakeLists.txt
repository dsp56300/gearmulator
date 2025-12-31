cmake_minimum_required(VERSION 3.10)

project(JE8086TestConsole)

add_executable(JE8086TestConsole)

set(SOURCES
	jeTestConsole.cpp
)

target_sources(JE8086TestConsole PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

createMacSetupScript("JE8086TestConsole")

target_link_libraries(JE8086TestConsole PUBLIC jeLib)

if(UNIX AND NOT APPLE)
	target_link_libraries(JE8086TestConsole PUBLIC -static-libgcc -static-libstdc++)
endif()

install(TARGETS JE8086TestConsole DESTINATION . COMPONENT JE8086TestConsole)

if(APPLE)
	installMacSetupScript(. JE8086TestConsole)
endif()

set_property(TARGET JE8086TestConsole PROPERTY FOLDER "JE8086")

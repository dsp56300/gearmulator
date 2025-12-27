cmake_minimum_required(VERSION 3.10)

project(jeTestConsole)

add_executable(jeTestConsole)

set(SOURCES
	jeTestConsole.cpp
)

target_sources(jeTestConsole PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

createMacSetupScript("jeTestConsole")

target_link_libraries(jeTestConsole PUBLIC jeLib)

if(UNIX AND NOT APPLE)
	target_link_libraries(jeTestConsole PUBLIC -static-libgcc -static-libstdc++)
endif()

install(TARGETS jeTestConsole DESTINATION . COMPONENT JE8086TestConsole)

if(APPLE)
	installMacSetupScript(. JETestConsole)
endif()

set_property(TARGET jeTestConsole PROPERTY FOLDER "JE8086")

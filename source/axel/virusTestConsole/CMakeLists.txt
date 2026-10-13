cmake_minimum_required(VERSION 3.10)

project(virusTestConsole)

add_executable(virusTestConsole)

set(SOURCES
	virusTestConsole.cpp
)

target_sources(virusTestConsole PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(virusTestConsole PUBLIC virusConsoleLib)

# start scripts that play a ROM preset
if(MSVC)
	set(deployDir ${CMAKE_SOURCE_DIR}/deploy/win/)
else()
	set(deployDir ${CMAKE_SOURCE_DIR}/deploy/linux/)
endif()

if(${gearmulator_SYNTH_OSIRUS})
	tus_installConsole(virusTestConsole COMPONENT OsirusTestConsole EXTRA_DIR ${deployDir})
endif()

if(${gearmulator_SYNTH_OSTIRUS})
	tus_installConsole(virusTestConsole COMPONENT OsTIrusTestConsole EXTRA_DIR ${deployDir})
endif()

set_property(TARGET virusTestConsole PROPERTY FOLDER "Virus")

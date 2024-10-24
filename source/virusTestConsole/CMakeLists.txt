cmake_minimum_required(VERSION 3.10)

project(virusTestConsole)

add_executable(virusTestConsole)

set(SOURCES
	virusTestConsole.cpp
)

target_sources(virusTestConsole PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(virusTestConsole PUBLIC virusConsoleLib)

if(UNIX AND NOT APPLE)
	target_link_libraries(virusTestConsole PUBLIC -static-libgcc -static-libstdc++)
endif()

createMacSetupScript("virusTestConsole")

if(${gearmulator_SYNTH_OSIRUS})
	install(TARGETS virusTestConsole DESTINATION . COMPONENT OsirusTestConsole)

	if(MSVC)
		install(DIRECTORY ${CMAKE_SOURCE_DIR}/deploy/win/ DESTINATION . COMPONENT OsirusTestConsole)
	else()
		install(DIRECTORY ${CMAKE_SOURCE_DIR}/deploy/linux/ DESTINATION . COMPONENT OsirusTestConsole)
	endif()
	if(APPLE)
		installMacSetupScript(. OsirusTestConsole)
	endif()
endif()

if(${gearmulator_SYNTH_OSTIRUS})
	install(TARGETS virusTestConsole DESTINATION . COMPONENT OsTIrusTestConsole)

	if(MSVC)
		install(DIRECTORY ${CMAKE_SOURCE_DIR}/deploy/win/ DESTINATION . COMPONENT OsTIrusTestConsole)
	else()
		install(DIRECTORY ${CMAKE_SOURCE_DIR}/deploy/linux/ DESTINATION . COMPONENT OsTIrusTestConsole)
	endif()
	if(APPLE)
		installMacSetupScript(. OsTIrusTestConsole)
	endif()
endif()

set_property(TARGET virusTestConsole PROPERTY FOLDER "Virus")

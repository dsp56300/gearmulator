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

install(TARGETS virusTestConsole DESTINATION . COMPONENT testConsole)

if(MSVC)
	install(DIRECTORY deploy/win/ DESTINATION . COMPONENT testConsole)
else()
	install(DIRECTORY deploy/linux/ DESTINATION . COMPONENT testConsole)
endif()

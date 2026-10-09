cmake_minimum_required(VERSION 3.10)

project(bridgeLib)

add_library(bridgeLib STATIC)

set(SOURCES
	audioBuffers.cpp audioBuffers.h
	command.cpp command.h
	commandReader.cpp commandReader.h
	commands.cpp commands.h
	commandStruct.cpp commandStruct.h
	commandWriter.cpp commandWriter.h
	error.cpp error.h
	tcpConnection.cpp tcpConnection.h
	types.h
)

target_sources(bridgeLib PRIVATE ${SOURCES})

source_group("source" FILES ${SOURCES})

target_link_libraries(bridgeLib PUBLIC networkLib synthLib)

target_include_directories(bridgeLib PUBLIC ../)
set_property(TARGET bridgeLib PROPERTY FOLDER "Bridge")

if(BUILD_TESTING)
	add_executable(bridgeLib_test bridgeLib_test.cpp)
	target_link_libraries(bridgeLib_test PRIVATE bridgeLib)
	set_property(TARGET bridgeLib_test PROPERTY FOLDER "Tests")
	add_test(NAME bridgeLib COMMAND bridgeLib_test)
endif()

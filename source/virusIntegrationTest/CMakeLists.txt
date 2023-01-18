cmake_minimum_required(VERSION 3.10)

project(virusIntegrationTest)

add_executable(virusIntegrationTest)

set(SOURCES
	integrationTest.cpp integrationTest.h
	../dsp56300/source/disassemble/commandline.cpp
	../dsp56300/source/disassemble/commandline.h
)

target_sources(virusIntegrationTest PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(virusIntegrationTest PUBLIC virusConsoleLib)

add_test(NAME virusIntegrationTests COMMAND ${CMAKE_COMMAND} 
	-DTEST_RUNNER=$<TARGET_FILE:virusIntegrationTest> 
	-DROOT_DIR=${CMAKE_BINARY_DIR} 
	-P ${CMAKE_CURRENT_SOURCE_DIR}/runTest.cmake)
set_tests_properties(virusIntegrationTests PROPERTIES LABELS "IntegrationTest")

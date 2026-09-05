cmake_minimum_required(VERSION 3.10)

project(synthLibTests VERSION ${CMAKE_PROJECT_VERSION})

set(SOURCES
	synthLibTests.cpp
	midiToSysexTest.cpp
)

add_executable(synthLibTests)

target_sources(synthLibTests PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(synthLibTests PUBLIC synthLib)

add_test(NAME synthLibTests COMMAND synthLibTests)
set_tests_properties(synthLibTests PROPERTIES LABELS "UnitTest")

set_property(TARGET synthLibTests PROPERTY FOLDER "Gearmulator")

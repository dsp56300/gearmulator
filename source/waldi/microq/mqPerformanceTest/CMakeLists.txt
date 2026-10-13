cmake_minimum_required(VERSION 3.10)

project(mqPerformanceTest)

add_executable(mqPerformanceTest)

set(SOURCES
	mqPerformanceTest.cpp
)

target_sources(mqPerformanceTest PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(mqPerformanceTest PUBLIC mqConsoleLib)

tus_installConsole(mqPerformanceTest COMPONENT VavraPerformanceTest)

set_property(TARGET mqPerformanceTest PROPERTY FOLDER "Vavra")

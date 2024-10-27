cmake_minimum_required(VERSION 3.10)

project(mqPerformanceTest)

add_executable(mqPerformanceTest)

set(SOURCES
	mqPerformanceTest.cpp
)

target_sources(mqPerformanceTest PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

createMacSetupScript("mqPerformanceTest")

target_link_libraries(mqPerformanceTest PUBLIC mqConsoleLib)

if(UNIX AND NOT APPLE)
	target_link_libraries(mqPerformanceTest PUBLIC -static-libgcc -static-libstdc++)
endif()

install(TARGETS mqPerformanceTest DESTINATION . COMPONENT VavraPerformanceTest)

if(APPLE)
	installMacSetupScript(. VavraPerformanceTest)
endif()

set_property(TARGET mqPerformanceTest PROPERTY FOLDER "Vavra")

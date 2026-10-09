cmake_minimum_required(VERSION 3.10)

project(virusArpTest)

add_executable(virusArpTest)

set(SOURCES virusArpTest.cpp)

target_sources(virusArpTest PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(virusArpTest PUBLIC virusLib)

set_property(TARGET virusArpTest PROPERTY FOLDER "Virus")

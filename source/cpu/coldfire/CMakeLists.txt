cmake_minimum_required(VERSION 3.15)

project(coldfire)

# ColdFire core and the MCF5206e chip model. More than one manufacturer's devices
# use this core, which is why it lives under cpu/ rather than under a single one.

add_library(coldfire STATIC)

set(SOURCES
	cfCpu.cpp cfCpu.h
	mcf5206e.cpp mcf5206e.h
)

target_sources(coldfire PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_include_directories(coldfire PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_property(TARGET coldfire PROPERTY FOLDER "cpu")

# differential test against Musashi's 68020 plus ColdFire specifics
add_executable(coldfireTest coldfireTest.cpp)
target_link_libraries(coldfireTest PRIVATE coldfire 68kEmu)
set_property(TARGET coldfireTest PROPERTY FOLDER "cpu")
add_test(NAME coldfireTest COMMAND coldfireTest)

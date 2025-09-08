cmake_minimum_required(VERSION 3.10)

project(jeLib)

add_library(jeLib STATIC)

set(SOURCES
	jetypes.h
	device.cpp
	device.h
	jemiditypes.h
)

target_sources(jeLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

set_property(TARGET jeLib PROPERTY FOLDER "JE8086")

target_link_libraries(jeLib PUBLIC synthLib hardwareLib)

target_include_directories(jeLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

#target_compile_options(jeLib PRIVATE -fsanitize=thread)
#target_link_options(jeLib PUBLIC -fsanitize=thread)

cmake_minimum_required(VERSION 3.10)

project(gpLib)

add_library(gpLib STATIC)

set(SOURCES
	gp.cpp gp.h
	gp_effects.cpp gp_internal.h
	../pcmInterpolation.h
)

target_sources(gpLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

set_property(TARGET gpLib PROPERTY FOLDER "Ronaldo")

target_include_directories(gpLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../..)

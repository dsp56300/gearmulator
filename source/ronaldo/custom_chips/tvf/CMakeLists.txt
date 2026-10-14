cmake_minimum_required(VERSION 3.10)

project(tvfLib)

add_library(tvfLib STATIC)

set(SOURCES
	tvf_emulator.cpp tvf_emulator.h
)

target_sources(tvfLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

set_property(TARGET tvfLib PROPERTY FOLDER "Ronaldo")

target_include_directories(tvfLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)

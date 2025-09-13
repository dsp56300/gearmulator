cmake_minimum_required(VERSION 3.10)

project(rLib)

add_library(rLib STATIC)

set(SOURCES
	storage.cpp storage.h
)

target_sources(rLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

set_property(TARGET rLib PROPERTY FOLDER "JE8086")

target_include_directories(rLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

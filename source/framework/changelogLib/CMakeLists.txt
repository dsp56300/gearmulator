cmake_minimum_required(VERSION 3.10)
project(changelogLib)

add_library(changelogLib STATIC)

set(SOURCES
	changelog.cpp changelog.h
)

target_sources(changelogLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

set_property(TARGET changelogLib PROPERTY FOLDER "Gearmulator")

target_include_directories(changelogLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

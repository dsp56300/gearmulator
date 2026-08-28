cmake_minimum_required(VERSION 3.10)

project(virusLibImport)

add_library(virusLibImport STATIC)

set(SOURCES
	TIControlStateDecoder.cpp TIControlStateDecoder.h
)

target_sources(virusLibImport PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(virusLibImport PUBLIC synthLib)
target_include_directories(virusLibImport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)

set_property(TARGET virusLibImport PROPERTY FOLDER "Virus")

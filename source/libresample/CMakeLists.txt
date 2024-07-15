cmake_minimum_required(VERSION 3.10)
project(resample)

set(SOURCES
	src/filterkit.c
	src/resample.c
	src/resamplesubs.c)

add_library(resample STATIC ${SOURCES})
set_property(TARGET resample PROPERTY FOLDER "Gearmulator")

target_include_directories(resample PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

cmake_minimum_required(VERSION 3.10)
project(resample)

set(SOURCES
	src/filterkit.c
	src/resample.c
	src/resamplesubs.c)

add_library(resample STATIC ${SOURCES})
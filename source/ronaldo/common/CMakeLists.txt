cmake_minimum_required(VERSION 3.10)

project(rLib)

add_library(rLib STATIC)

set(SOURCES
	storage.cpp storage.h
	romDescramble.h
	jit_x64_types.h
	jit_x64_regpool.h
	jit_x64_regpool.cpp
)

target_sources(rLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

set_property(TARGET rLib PROPERTY FOLDER "Ronaldo")

target_link_libraries(rLib PUBLIC asmjit)

target_include_directories(rLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

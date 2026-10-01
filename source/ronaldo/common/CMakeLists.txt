cmake_minimum_required(VERSION 3.10)

project(rLib)

add_library(rLib STATIC)

set(SOURCES
	boardThread.h
	storage.cpp storage.h
	romDescramble.h
	jit_x64_types.h
	jit_x64_regpool.h
	jit_x64_regpool.cpp
)

target_sources(rLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

set_property(TARGET rLib PROPERTY FOLDER "Ronaldo")

target_link_libraries(rLib PUBLIC asmjit synthLib dsp56kBase)

target_include_directories(rLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(BUILD_TESTING)
	add_executable(boardThreadTest boardThread_test.cpp)
	target_link_libraries(boardThreadTest PRIVATE rLib)
	set_property(TARGET boardThreadTest PROPERTY FOLDER "Ronaldo/Tests")
	add_test(NAME boardThreadTest COMMAND boardThreadTest)
endif()

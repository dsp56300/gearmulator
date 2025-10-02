cmake_minimum_required(VERSION 3.10)

project(esp)

add_library(esp STATIC)

set(SOURCES
	esp.hpp
	esp_jit.cpp
	esp_jit.h
	esp_jit_arm64.cpp
	esp_jit_arm64.h
	esp_jit_types.h
	esp_jit_x64.cpp
	esp_jit_x64.h
	esp_jit_x64_pool.cpp
	esp_jit_x64_pool.h
	esp_jit_x64_types.h
	esp_opt.hpp
)

target_sources(esp PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

set_property(TARGET esp PROPERTY FOLDER "Ronaldo")

target_link_libraries(esp PUBLIC asmjit)

target_include_directories(esp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

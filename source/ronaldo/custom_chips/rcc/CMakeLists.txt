cmake_minimum_required(VERSION 3.15)

project(rccLib)
add_library(rccLib STATIC rcc.cpp rccBus.cpp rcc.h)
target_compile_features(rccLib PUBLIC cxx_std_17)
target_include_directories(rccLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set_property(TARGET rccLib PROPERTY FOLDER "Ronaldo")

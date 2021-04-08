cmake_minimum_required(VERSION 3.15)

# build a fat binary that runs on both intel and the new Apple M1 chip
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.9" CACHE STRING "Minimum OS X deployment version")

project(virusEmu VERSION 0.0.1)

include(base.cmake)

add_subdirectory(source/dsp56300/source)
add_subdirectory(source/virusLib)

add_executable(virusTestConsole)

target_sources(virusTestConsole PRIVATE source/virusTestConsole/virusTestConsole.cpp)
target_link_libraries(virusTestConsole PUBLIC virusLib)

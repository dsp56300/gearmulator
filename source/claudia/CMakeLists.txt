cmake_minimum_required(VERSION 3.15)

if(${CMAKE_PROJECT_NAME}_SYNTH_NODALRED2X)
	add_subdirectory(n2x)
endif()

option(gearmulator_BUILD_NMRACK "Build the experimental nmrack emulator" OFF)
if(gearmulator_BUILD_NMRACK)
    add_subdirectory(nmrack)
endif()

if(${CMAKE_PROJECT_NAME}_SYNTH_NORDMICROMODULAR)
	add_subdirectory(nmm)
endif()

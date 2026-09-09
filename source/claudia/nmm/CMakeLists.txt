cmake_minimum_required(VERSION 3.15)
add_subdirectory(nmmLib)
add_subdirectory(nmmTestConsole)

if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN)
	add_subdirectory(nmmJucePlugin)
endif()

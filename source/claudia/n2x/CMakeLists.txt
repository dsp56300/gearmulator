cmake_minimum_required(VERSION 3.15)

add_subdirectory(n2xLib)
add_subdirectory(n2xTestConsole)

if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN)
	add_subdirectory(n2xJucePlugin)
endif()

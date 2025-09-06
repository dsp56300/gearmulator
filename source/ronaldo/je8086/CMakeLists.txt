cmake_minimum_required(VERSION 3.15)

add_subdirectory(jeLib)
#add_subdirectory(jeTestConsole)

if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN)
	add_subdirectory(jeJucePlugin)
endif()

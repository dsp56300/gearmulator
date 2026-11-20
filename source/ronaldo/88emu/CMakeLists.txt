cmake_minimum_required(VERSION 3.15)

add_subdirectory(88lib)
add_subdirectory(88emuClapHeadless)

if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN AND USE_Standalone)
	add_subdirectory(88emuplayer)
endif()

cmake_minimum_required(VERSION 3.10)
project(baseLib)

add_library(baseLib STATIC)

set(SOURCES
	binarystream.cpp binarystream.h
	commandline.cpp commandline.h
	configFile.cpp configFile.h
	filesystem.cpp filesystem.h
	hybridcontainer.h
	md5.cpp md5.h
	propertyMap.cpp propertyMap.h
	semaphore.h
)

target_sources(baseLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

if(NOT MSVC)
	target_link_libraries(baseLib PUBLIC dl)
endif()

set_property(TARGET baseLib PROPERTY FOLDER "Gearmulator")

target_include_directories(baseLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

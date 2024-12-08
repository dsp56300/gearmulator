cmake_minimum_required(VERSION 3.10)

project(bridgeClient)

add_library(bridgeClient STATIC)

set(SOURCES
	deviceConnection.cpp deviceConnection.h
	export.cpp export.h
	types.h
	plugin.h
	remoteDevice.cpp remoteDevice.h
	serverList.cpp serverList.h
	udpClient.cpp udpClient.h
)

target_sources(bridgeClient PRIVATE ${SOURCES})

source_group("source" FILES ${SOURCES})

target_link_libraries(bridgeClient PUBLIC bridgeLib)

target_include_directories(bridgeClient PUBLIC ../)
set_property(TARGET bridgeClient PROPERTY FOLDER "Bridge")

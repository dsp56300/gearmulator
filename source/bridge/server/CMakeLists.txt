cmake_minimum_required(VERSION 3.10)

project(bridgeServer)

add_executable(bridgeServer)

set(SOURCES
	bridgeServer.cpp
	clientConnection.cpp clientConnection.h
	config.cpp config.h
	server.cpp server.h
	import.cpp import.h
	romPool.cpp romPool.h
	udpServer.cpp udpServer.h
)

target_sources(bridgeServer PRIVATE ${SOURCES})

source_group("source" FILES ${SOURCES})

target_link_libraries(bridgeServer PUBLIC bridgeLib)

set_property(TARGET bridgeServer PROPERTY FOLDER "Bridge")
set_property(TARGET bridgeServer PROPERTY OUTPUT_NAME "dsp56300EmuServer")

install(TARGETS bridgeServer DESTINATION . COMPONENT DSPBridgeServer)

cmake_minimum_required(VERSION 3.10)

project(networkLib)

add_library(networkLib STATIC)

set(SOURCES
	exception.cpp exception.h
	logging.cpp logging.h
	networkThread.cpp
	networkThread.h
	stream.cpp
	stream.h
	tcpClient.cpp
	tcpClient.h
	tcpConnection.cpp
	tcpConnection.h
	tcpServer.cpp
	tcpServer.h
	tcpStream.cpp
	tcpStream.h
	udpClient.cpp
	udpClient.h
	udpServer.cpp
	udpServer.h
)

target_sources(networkLib PRIVATE ${SOURCES})

source_group("source" FILES ${SOURCES})

target_link_libraries(networkLib PUBLIC ptypes)
if(MSVC)
	target_link_libraries(networkLib PUBLIC ws2_32)
endif()

target_include_directories(networkLib PUBLIC ../)
set_property(TARGET networkLib PROPERTY FOLDER "Networking")

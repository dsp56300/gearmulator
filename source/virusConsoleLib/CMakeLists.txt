cmake_minimum_required(VERSION 3.10)
project(virusConsoleLib)

add_library(virusConsoleLib STATIC)

set(SOURCES
	audioProcessor.cpp audioProcessor.h
	esaiListener.cpp esaiListener.h
	esaiListenerToCallback.cpp esaiListenerToCallback.h
	esaiListenerToFile.cpp esaiListenerToFile.h
	consoleApp.cpp consoleApp.h
)

target_sources(virusConsoleLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(virusConsoleLib PUBLIC virusLib)

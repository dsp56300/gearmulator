cmake_minimum_required(VERSION 3.15)
project(jucePluginLib VERSION ${CMAKE_PROJECT_VERSION}) 

set(SOURCES
	controller.cpp controller.h
	midipacket.cpp midipacket.h
	parameter.cpp parameter.h
	parameterdescription.cpp parameterdescription.h
	parameterdescriptions.cpp parameterdescriptions.h
	parameterlink.cpp parameterlink.h
)

add_library(jucePluginLib STATIC)

target_sources(jucePluginLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_include_directories(jucePluginLib PUBLIC ../JUCE/modules)
target_compile_definitions(jucePluginLib PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)

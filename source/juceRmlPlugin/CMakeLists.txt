cmake_minimum_required(VERSION 3.15)
project(juceRmlPlugin VERSION ${CMAKE_PROJECT_VERSION}) 

set(SOURCES
	rmlPlugin.cpp rmlPlugin.h
	rmlParameterBinding.cpp rmlParameterBinding.h
	rmlParameterRef.cpp rmlParameterRef.h
)

add_library(juceRmlPlugin STATIC)

target_sources(juceRmlPlugin PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(juceRmlPlugin PUBLIC juceRmlUi jucePluginLib)

target_include_directories(juceRmlPlugin PUBLIC ../JUCE/modules)
target_compile_definitions(juceRmlPlugin PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
set_property(TARGET juceRmlPlugin PROPERTY FOLDER "Gearmulator")

target_include_directories(juceRmlPlugin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

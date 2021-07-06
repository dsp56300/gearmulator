cmake_minimum_required(VERSION 3.10)
project(virusFilterVst2)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.h.in ${CMAKE_CURRENT_SOURCE_DIR}/version.h)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/versionRC.h.in ${CMAKE_CURRENT_SOURCE_DIR}/versionRC.h)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist)

add_library(virusFilterVst2 STATIC)

target_sources(virusFilterVst2 PRIVATE version.h version.h.in vstAudioEffect.cpp vstAudioEffect.h)

if(WIN32)
	target_sources(virusFilterVst2 PRIVATE version.rc)
endif()

target_include_directories(virusFilterVst2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../vstsdk2.4.2/)

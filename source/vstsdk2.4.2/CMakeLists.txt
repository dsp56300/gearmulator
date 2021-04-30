cmake_minimum_required(VERSION 3.10)
project(vstsdk2)

set(SOURCES
	public.sdk/source/vst2.x/audioeffect.cpp
	public.sdk/source/vst2.x/audioeffectx.cpp
#	public.sdk/source/vst2.x/vstplugmain.cpp
	)

add_library(vstsdk2 STATIC ${SOURCES})

target_include_directories(vstsdk2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

cmake_minimum_required(VERSION 3.15)

# build a fat binary that runs on both intel and the new Apple M1 chip
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.9" CACHE STRING "Minimum OS X deployment version")

project(virusEmu VERSION 0.0.1)

include(base.cmake)

add_subdirectory(source/dsp56300/source)
add_subdirectory(source/virusLib)
add_subdirectory(source/portmidi)
add_subdirectory(source/portaudio)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/source/vstsdk2.4.2/public.sdk/source/vst2.x/audioeffect.h)
	set(VIRUS_VST2 true)
else()
	set(VIRUS_VST2 false)
endif()

if(VIRUS_VST2)
	add_subdirectory(source/vstsdk2.4.2)
	add_subdirectory(source/vst2)
endif()

# ----------------- Test Console

add_executable(virusTestConsole)
target_sources(virusTestConsole PRIVATE source/virusTestConsole/virusTestConsole.cpp)
target_link_libraries(virusTestConsole PUBLIC virusLib)

# ----------------- Standalone

add_executable(virusStandalone)
target_sources(virusStandalone PRIVATE source/virusStandalone/virusStandalone.cpp)
target_link_libraries(virusStandalone PUBLIC virusLib portaudio_static)

# ----------------- VST2 Plugin

if(VIRUS_VST2)
	add_library(virusVst2dll MODULE)

	target_sources(virusVst2dll PRIVATE source/vst2/vstMain.cpp)

	if(MSVC)
		target_sources(virusVst2dll PRIVATE source/vstsdk2.4.2/public.sdk/samples/vst2.x/win/vstplug.def source/vst2/version.rc)
	endif()

	target_include_directories(virusVst2dll PRIVATE source/vstsdk2.4.2/)

	target_link_libraries(virusVst2dll virusVst2 vstsdk2 virusLib)

	if(MSVC)
		set_target_properties(virusVst2dll PROPERTIES OUTPUT_NAME "VirusEmuVST2${CMAKE_VS_PLATFORM_NAME}")
	else()
		set_target_properties(virusVst2dll PROPERTIES OUTPUT_NAME "VirusEmuVST2")
	endif()
endif()

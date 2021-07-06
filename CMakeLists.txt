cmake_minimum_required(VERSION 3.15)

# build a fat binary that runs on both intel and the new Apple M1 chip
#set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
set(CMAKE_OSX_ARCHITECTURES "x86_64")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.9" CACHE STRING "Minimum OS X deployment version")

project(virusEmu VERSION 0.0.2)

include(base.cmake)

set(ASMJIT_STATIC TRUE)

add_subdirectory(source/dsp56300/source)
add_subdirectory(source/virusLib)
add_subdirectory(source/portmidi)
add_subdirectory(source/portaudio)
add_subdirectory(source/libresample)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/source/vstsdk2.4.2/public.sdk/source/vst2.x/audioeffect.h)
	set(VIRUS_VST2 true)
	set(VIRUS_VST2Filter true)
else()
	set(VIRUS_VST2 false)
	set(VIRUS_VST2Filter false)
endif()


if(VIRUS_VST2)
	add_subdirectory(source/vstsdk2.4.2)
	add_subdirectory(source/vst2)
	add_subdirectory(source/vst2filter)
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

	target_sources(virusVst2dll PRIVATE source/vstsdk2.4.2/public.sdk/source/vst2.x/vstplugmain.cpp)
	if(MSVC)
		target_sources(virusVst2dll PRIVATE source/vstsdk2.4.2/public.sdk/samples/vst2.x/win/vstplug.def source/vst2/version.rc)
	elseif(APPLE)
		set_target_properties(virusVst2dll PROPERTIES BUNDLE TRUE)
		set_target_properties(virusVst2dll PROPERTIES CXX_VISIBILITY_PRESET default)
		set_target_properties(virusVst2dll PROPERTIES CMAKE_C_VISIBILITY_PRESET default)
		set_target_properties(virusVst2dll PROPERTIES XCODE_ATTRIBUTE_INFOPLIST_FILE "source/vst2/Info.plist")
  		set_target_properties(virusVst2dll PROPERTIES XCODE_ATTRIBUTE_GENERATE_PKGINFO_FILE "YES")
		set_target_properties(virusVst2dll PROPERTIES XCODE_ATTRIBUTE_WRAPPER_EXTENSION "vst")
		set_target_properties(virusVst2dll PROPERTIES XCODE_ATTRIBUTE_GENERATE_MASTER_OBJECT_FILE "YES")
		set_target_properties(virusVst2dll PROPERTIES XCODE_ATTRIBUTE_OTHER_LDFLAGS "-all_load")
	endif()

	target_include_directories(virusVst2dll PRIVATE source/vstsdk2.4.2/)

	target_link_libraries(virusVst2dll virusVst2 vstsdk2 virusLib)

	if(MSVC)
		set_target_properties(virusVst2dll PROPERTIES OUTPUT_NAME "VirusEmuVST2${CMAKE_VS_PLATFORM_NAME}")
	else()
		set_target_properties(virusVst2dll PROPERTIES OUTPUT_NAME "VirusVST2")
	endif()
endif()

# ----------------- VST2 Filter Plugin

if(VIRUS_VST2Filter)
	add_library(virusVstFilter MODULE)

	target_sources(virusVstFilter PRIVATE source/vstsdk2.4.2/public.sdk/source/vst2.x/vstplugmain.cpp)
	if(MSVC)
		target_sources(virusVstFilter PRIVATE source/vstsdk2.4.2/public.sdk/samples/vst2.x/win/vstplug.def source/vst2/version.rc)
	elseif(APPLE)
		set_target_properties(virusVstFilter PROPERTIES BUNDLE TRUE)
		set_target_properties(virusVstFilter PROPERTIES CXX_VISIBILITY_PRESET default)
		set_target_properties(virusVstFilter PROPERTIES CMAKE_C_VISIBILITY_PRESET default)
		set_target_properties(virusVstFilter PROPERTIES XCODE_ATTRIBUTE_INFOPLIST_FILE "source/vst2filter/Info.plist")
  		set_target_properties(virusVstFilter PROPERTIES XCODE_ATTRIBUTE_GENERATE_PKGINFO_FILE "YES")
		set_target_properties(virusVstFilter PROPERTIES XCODE_ATTRIBUTE_WRAPPER_EXTENSION "vst")
		set_target_properties(virusVstFilter PROPERTIES XCODE_ATTRIBUTE_GENERATE_MASTER_OBJECT_FILE "YES")
		set_target_properties(virusVstFilter PROPERTIES XCODE_ATTRIBUTE_OTHER_LDFLAGS "-all_load")
	endif()

	target_include_directories(virusVstFilter PRIVATE source/vstsdk2.4.2/)

	target_link_libraries(virusVstFilter virusFilterVst2 vstsdk2)

	if(MSVC)
		set_target_properties(virusVstFilter PROPERTIES OUTPUT_NAME "VirusVST2Filter${CMAKE_VS_PLATFORM_NAME}")
	else()
		set_target_properties(virusVstFilter PROPERTIES OUTPUT_NAME "VirusVST2Filter")
	endif()
endif()
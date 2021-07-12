cmake_minimum_required(VERSION 3.15)

# build a fat binary that runs on both intel and the new Apple M1 chip
#set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
set(CMAKE_OSX_ARCHITECTURES "x86_64")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.9" CACHE STRING "Minimum OS X deployment version")

project(virusEmu VERSION 1.1.4)

include(base.cmake)

set(ASMJIT_STATIC TRUE)

add_subdirectory(source/dsp56300/source)
add_subdirectory(source/synthLib)
add_subdirectory(source/virusLib)
add_subdirectory(source/libresample)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/source/vstsdk2.4.2/public.sdk/source/vst2.x/audioeffect.h)
	set(VIRUS_VST2Filter false)	# skip for now
	set(JUCE_GLOBAL_VST2_SDK_PATH ${CMAKE_CURRENT_SOURCE_DIR}/source/vstsdk2.4.2/)
else()
	set(VIRUS_VST2Filter false)
endif()

add_subdirectory(source/ctrlr/JUCE)
#add_subdirectory(source/ctrlr)

# ----------------- Juce based audio plugin

set_property(GLOBAL PROPERTY USE_FOLDERS YES)
set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON CACHE BOOL "" FORCE)

add_subdirectory(source/jucePlugin)

if(UNIX AND NOT APPLE)
	install(TARGETS jucePlugin_VST LIBRARY DESTINATION /usr/local/lib/lxvst/ COMPONENT plugin)
endif()

# ----------------- Test Console

add_executable(virusTestConsole)
target_sources(virusTestConsole PRIVATE source/virusTestConsole/virusTestConsole.cpp)
target_link_libraries(virusTestConsole PUBLIC virusLib)

# ----------------- VST2 Filter Plugin

if(VIRUS_VST2Filter)
	add_subdirectory(source/vstsdk2.4.2)
	add_subdirectory(source/vst2filter)

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

# ----------------- CPack

set(CPACK_COMPONENTS_ALL plugin)
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_RPM_COMPONENT_INSTALL ON)

set(CPACK_PACKAGE_CONTACT "The Usual Suspects")
set(CPACK_PACKAGE_VENDOR "The Usual Suspects")
set(CPACK_PACKAGE_NAME "DSP56300Emu")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "DSP 56300 family emulator audio plugin")

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "The Usual Suspects")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://dsp56300.wordpress.com")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

set(CPACK_RPM_PACKAGE_AUTOREQ "yes")
set(CPACK_RPM_PACKAGE_URL ${CPACK_DEBIAN_PACKAGE_HOMEPAGE})
set(CPACK_RPM_PACKAGE_DESCRIPTION ${CPACK_PACKAGE_DESCRIPTION_SUMMARY})

include(CPack)

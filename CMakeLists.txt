cmake_minimum_required(VERSION 3.15)

# build a fat binary that runs on both intel and the new Apple M1 chip
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
#set(CMAKE_OSX_ARCHITECTURES "x86_64")

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.9" CACHE STRING "Minimum OS X deployment version")

project(gearmulator VERSION 1.1.9)

include(base.cmake)

set(ASMJIT_STATIC TRUE)
option(${PROJECT_NAME}_BUILD_JUCEPLUGIN "Build Juce plugin" on)
add_subdirectory(source/dsp56300/source)
add_subdirectory(source/synthLib)
add_subdirectory(source/virusLib)
add_subdirectory(source/libresample)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/source/vstsdk2.4.2/public.sdk/source/vst2.x/audioeffect.h)
	set(JUCE_GLOBAL_VST2_SDK_PATH ${CMAKE_CURRENT_SOURCE_DIR}/source/vstsdk2.4.2/)
endif()

# ----------------- Juce based audio plugin

set_property(GLOBAL PROPERTY USE_FOLDERS YES)
if(${PROJECT_NAME}_BUILD_JUCEPLUGIN)
	set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON CACHE BOOL "" FORCE)
	add_subdirectory(source/JUCE)
	add_subdirectory(source/jucePlugin)
	if(JUCE_GLOBAL_VST2_SDK_PATH)
		if(MSVC OR APPLE)
			install(TARGETS jucePlugin_VST DESTINATION . COMPONENT VST2)
			install(TARGETS jucePlugin_VST3 DESTINATION . COMPONENT VST3)
			if(APPLE)
				install(TARGETS jucePlugin_AU DESTINATION . COMPONENT AU)
			endif()
		elseif(UNIX)
			install(TARGETS jucePlugin_VST LIBRARY DESTINATION /usr/local/lib/lxvst/ COMPONENT VST2)
			install(TARGETS jucePlugin_VST3 LIBRARY DESTINATION /usr/local/lib/vst3/ COMPONENT VST3)
		endif()
	endif()
endif()

# ----------------- Test Console

add_executable(virusTestConsole)
target_sources(virusTestConsole PRIVATE source/virusTestConsole/virusTestConsole.cpp)
target_link_libraries(virusTestConsole PUBLIC virusLib)

if(UNIX AND NOT APPLE)
	target_link_libraries(virusTestConsole PUBLIC -static-libgcc -static-libstdc++)
endif()

install(TARGETS virusTestConsole DESTINATION . COMPONENT testConsole)

if(MSVC)
	install(DIRECTORY deploy/win/ DESTINATION . COMPONENT testConsole)
else()
	install(DIRECTORY deploy/linux/ DESTINATION . COMPONENT testConsole)
endif()

# ----------------- CPack

set(CPACK_COMPONENTS_ALL VST2 VST3 AU testConsole)

set(CPACK_COMPONENTS_GROUPING IGNORE)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)

set(CPACK_PACKAGE_CONTACT "The Usual Suspects")
set(CPACK_PACKAGE_VENDOR "The Usual Suspects")
set(CPACK_PACKAGE_NAME "DSP56300Emu")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "DSP 56300 family emulator audio plugin")

set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "The Usual Suspects")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://dsp56300.wordpress.com")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

set(CPACK_RPM_COMPONENT_INSTALL ON)
set(CPACK_RPM_PACKAGE_AUTOREQ "yes")
set(CPACK_RPM_PACKAGE_URL ${CPACK_DEBIAN_PACKAGE_HOMEPAGE})
set(CPACK_RPM_PACKAGE_DESCRIPTION ${CPACK_PACKAGE_DESCRIPTION_SUMMARY})

include(CPack)

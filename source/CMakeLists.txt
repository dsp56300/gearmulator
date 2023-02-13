cmake_minimum_required(VERSION 3.15)

option(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN "Build Juce plugin" on)
option(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN_CLAP "Build CLAP version of Juce plugin" on)

option(${CMAKE_PROJECT_NAME}_SYNTH_OSIRUS "Build Osirus" on)

# ----------------- DSP56300 emulator

set(ASMJIT_STATIC TRUE)
set(ASMJIT_NO_INSTALL TRUE)
set(BUILD_SHARED_LIBS OFF)

add_subdirectory(dsp56300/source)

# ----------------- Common libraries used by all synths

add_subdirectory(synthLib)
add_subdirectory(libresample)

# ----------------- Try to install VST2 SDK

include(findvst2.cmake)

# ----------------- Juce based audio plugin dependencies

set_property(GLOBAL PROPERTY USE_FOLDERS YES)
if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN)
	set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON CACHE BOOL "" FORCE)
	add_subdirectory(JUCE)
	if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN_CLAP)
		add_subdirectory(clap-juce-extensions)
	endif()
	add_subdirectory(jucePluginLib)
	add_subdirectory(juceUiLib)
	add_subdirectory(jucePluginEditorLib)
	
	include(juce.cmake)
endif()

# ----------------- Synth Osirus

if(${CMAKE_PROJECT_NAME}_SYNTH_OSIRUS)
	add_subdirectory(virusLib)
	add_subdirectory(virusConsoleLib)
	add_subdirectory(virusTestConsole)
	add_subdirectory(virusIntegrationTest)
	if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN)
		add_subdirectory(jucePlugin)
	endif()
endif()

cmake_minimum_required(VERSION 3.15)

option(${CMAKE_PROJECT_NAME}_SYNTH_OSIRUS "Build Osirus" on)
option(${CMAKE_PROJECT_NAME}_SYNTH_OSTIRUS "Build OsTIrus" on)
option(${CMAKE_PROJECT_NAME}_SYNTH_VAVRA "Build Vavra" on)
option(${CMAKE_PROJECT_NAME}_SYNTH_XENIA "Build Xenia" on)
option(${CMAKE_PROJECT_NAME}_SYNTH_NODALRED2X "Build NodalRed2x" on)
option(${CMAKE_PROJECT_NAME}_SYNTH_JE8086 "Build JE-8086" on)
option(${CMAKE_PROJECT_NAME}_SYNTH_88EMU "Build 88Emu" on)
option(${CMAKE_PROJECT_NAME}_COMPONENT_DSPBRIDGE "Build the DSPBridge server" on)

# ----------------- Add our cmake scripts to IDE

add_custom_target(cmakeScripts SOURCES
	cmake/base.cmake
	cmake/changelog.cmake
	cmake/exporttarget.cmake
	cmake/findasio.cmake
	cmake/findvst2.cmake
	cmake/juce.cmake
	cmake/macsetup.cmake
	cmake/macsetup.command.in
	cmake/runAuValidation.cmake
	cmake/skins.cmake
	cmake/skins.h.in
	cmake/xcodeversion.cmake)

# ----------------- Processor cores

set(ASMJIT_STATIC TRUE)
set(ASMJIT_NO_INSTALL TRUE)
set(BUILD_SHARED_LIBS OFF)

add_subdirectory(cpu)

# ----------------- Third party libraries

add_subdirectory(3rdparty)

include(cmake/macsetup.cmake)
include(cmake/skins.cmake)
include(cmake/exporttarget.cmake)
include(cmake/changelog.cmake)

# ----------------- Try to install VST2 SDK

include(cmake/findvst2.cmake)

# ----------------- Try to install ASIO SDK

include(cmake/findasio.cmake)

# ----------------- JUCE

set_property(GLOBAL PROPERTY USE_FOLDERS YES)
if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN)
	set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON CACHE BOOL "" FORCE)

	# juceUiLib and juceRmlUi need the module headers directly, everyone else
	# inherits them by linking one of those two
	set(JUCE_MODULES_DIR ${CMAKE_CURRENT_LIST_DIR}/3rdparty/JUCE/modules)

	add_subdirectory(3rdparty/JUCE EXCLUDE_FROM_ALL)
	if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN_CLAP)
		add_subdirectory(3rdparty/clap-juce-extensions)
	endif()
endif()

# ----------------- Framework: the libraries and tools all products share

add_subdirectory(framework)

if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN)
	include(cmake/juce.cmake)
endif()

# ----------------- Products, one folder per manufacturer

add_subdirectory(axel)
add_subdirectory(waldi)
add_subdirectory(claudia)
add_subdirectory(ronaldo)

# ----------------- Installer

option(${CMAKE_PROJECT_NAME}_BUILD_INSTALLER "Build TUS Plugin Manager" on)
if(${CMAKE_PROJECT_NAME}_BUILD_INSTALLER AND ${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN)
	add_subdirectory(framework/tools/installer)
endif()

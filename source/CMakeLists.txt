cmake_minimum_required(VERSION 3.15)

option(${CMAKE_PROJECT_NAME}_SYNTH_OSIRUS "Build Osirus" on)
option(${CMAKE_PROJECT_NAME}_SYNTH_OSTIRUS "Build OsTIrus" on)
option(${CMAKE_PROJECT_NAME}_SYNTH_VAVRA "Build Vavra" on)
option(${CMAKE_PROJECT_NAME}_SYNTH_XENIA "Build Xenia" on)
option(${CMAKE_PROJECT_NAME}_SYNTH_NODALRED2X "Build NodalRed2x" on)
option(${CMAKE_PROJECT_NAME}_SYNTH_JE8086 "Build JE-8086" on)
option(${CMAKE_PROJECT_NAME}_SYNTH_88EMU "Build 88Emu" on)

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

# ----------------- Tools

add_subdirectory(framework/tools/changelogGenerator)

# ----------------- DSP56300 emulator

set(ASMJIT_STATIC TRUE)
set(ASMJIT_NO_INSTALL TRUE)
set(BUILD_SHARED_LIBS OFF)

add_subdirectory(cpu/dsp56300/source)

# ----------------- Common libraries used by all synths

add_subdirectory(framework/baseLib)
add_subdirectory(framework/synthLib)
add_subdirectory(framework/tools/synthLibTests)
add_subdirectory(3rdparty/libresample)

add_subdirectory(3rdparty)

include(cmake/macsetup.cmake)
include(cmake/skins.cmake)
include(cmake/exporttarget.cmake)
include(cmake/changelog.cmake)

# ----------------- network bridge

add_subdirectory(3rdparty/ptypes EXCLUDE_FROM_ALL)
add_subdirectory(framework/networkLib EXCLUDE_FROM_ALL)
add_subdirectory(framework/tools/bridge)

# ----------------- MCP server

# MCP server is always built, but can be enabled/disabled at runtime via plugin settings

# ----------------- Try to install VST2 SDK

include(cmake/findvst2.cmake)

# ----------------- Try to install ASIO SDK

include(cmake/findasio.cmake)

# ----------------- Juce based audio plugin dependencies

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
	add_subdirectory(framework/juce/jucePluginLib EXCLUDE_FROM_ALL)
	add_subdirectory(framework/juce/mcpServerLib EXCLUDE_FROM_ALL)
	add_subdirectory(framework/juce/juceRmlUi)
	add_subdirectory(framework/juce/juceRmlPlugin)
	add_subdirectory(framework/juce/juceUiLib EXCLUDE_FROM_ALL)
	add_subdirectory(framework/juce/jucePluginEditorLib EXCLUDE_FROM_ALL)
	add_subdirectory(framework/juce/jucePluginData EXCLUDE_FROM_ALL)
	add_subdirectory(framework/tools/pluginTester)
	add_subdirectory(framework/tools/midiLearnTest)
	add_subdirectory(framework/tools/jucePluginLibTests)
	include(cmake/juce.cmake)
endif()

# ----------------- dependencies

if(${CMAKE_PROJECT_NAME}_SYNTH_88EMU)
	add_subdirectory(cpu/common)
	add_subdirectory(cpu/h8500)
	add_subdirectory(cpu/sh2)
endif()
add_subdirectory(cpu/h8s)
add_subdirectory(cpu/mc68k EXCLUDE_FROM_ALL)

# mc68k does not declare an include root of its own, so #include "mc68k/..."
# used to resolve only through the blanket source/ root that baseLib exported.
# Hand it the cpu/ root here until the submodule declares it itself.
target_include_directories(68kEmu PUBLIC ${CMAKE_CURRENT_LIST_DIR}/cpu)

add_subdirectory(framework/hardwareLib EXCLUDE_FROM_ALL)
add_subdirectory(waldi/common/wLib EXCLUDE_FROM_ALL)

# ----------------- Synths Osirus/OsTIrus

if(${CMAKE_PROJECT_NAME}_SYNTH_OSIRUS OR ${CMAKE_PROJECT_NAME}_SYNTH_OSTIRUS)
	add_subdirectory(axel/virusLib EXCLUDE_FROM_ALL)
	add_subdirectory(axel/virusConsoleLib EXCLUDE_FROM_ALL)
	add_subdirectory(axel/virusTestConsole)
	add_subdirectory(axel/virusIntegrationTest)
	add_subdirectory(axel/virusRomHashTest)
	if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN)
		add_subdirectory(axel/virusJucePlugin EXCLUDE_FROM_ALL)
		if(${CMAKE_PROJECT_NAME}_SYNTH_OSIRUS)
			add_subdirectory(axel/osirusJucePlugin)
		endif()
		if(${CMAKE_PROJECT_NAME}_SYNTH_OSTIRUS)
			add_subdirectory(axel/osTIrusJucePlugin)
		endif()
	endif()
endif()

# ----------------- Synth Vavra

if(${CMAKE_PROJECT_NAME}_SYNTH_VAVRA)
	add_subdirectory(waldi/microq/mqLib EXCLUDE_FROM_ALL)

	# needed for test console
	set(CPPTERMINAL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(CPPTERMINAL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
	set(CPPTERMINAL_ENABLE_TESING OFF CACHE BOOL "" FORCE)
	add_subdirectory(3rdparty/cpp-terminal EXCLUDE_FROM_ALL)

	if(NOT ANDROID)
		add_subdirectory(3rdparty/portmidi EXCLUDE_FROM_ALL)
	endif()

	set(PA_USE_ASIO OFF CACHE BOOL "" FORCE)
	add_subdirectory(3rdparty/portaudio EXCLUDE_FROM_ALL)

	add_subdirectory(waldi/microq/mqConsoleLib EXCLUDE_FROM_ALL)
	add_subdirectory(waldi/microq/mqTestConsole)
	add_subdirectory(waldi/microq/mqPerformanceTest)

	if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN)
		add_subdirectory(waldi/microq/mqJucePlugin)
	endif()
endif()

# The SC-88 player needs PortMidi for its MIDI bridge. We use
# 3rdparty/portmidi-latest specifically, not the older 3rdparty/portmidi tree that
# Vavra pulls in, because it exposes virtual-device creation (Pm_CreateVirtualInput/
# Output), which lets the player publish its own MIDI ports to the host instead of
# needing a loop-back driver. The two trees produce different target names
# (portmidi-static vs. portmidi) so they coexist when both Vavra and the SC-88 are on.
if(${CMAKE_PROJECT_NAME}_SYNTH_88EMU AND NOT ANDROID AND NOT TARGET portmidi)
	# Build static even if a parent set BUILD_SHARED_LIBS ON.
	set(_pm_prev_shared ${BUILD_SHARED_LIBS})
	set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
	add_subdirectory(3rdparty/portmidi-latest EXCLUDE_FROM_ALL)
	set(BUILD_SHARED_LIBS ${_pm_prev_shared} CACHE BOOL "" FORCE)
endif()

# ----------------- Synth Xenia

if(${CMAKE_PROJECT_NAME}_SYNTH_XENIA)
	add_subdirectory(waldi/xt/xtLib EXCLUDE_FROM_ALL)
	add_subdirectory(waldi/xt/xtTestConsole)

	if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN)
		add_subdirectory(waldi/xt/xtJucePlugin)
	endif()
endif()

# ----------------- all claudias

add_subdirectory(claudia)

# ----------------- all ronaldos

add_subdirectory(ronaldo)

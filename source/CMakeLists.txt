cmake_minimum_required(VERSION 3.15)

option(${CMAKE_PROJECT_NAME}_SYNTH_OSIRUS "Build Osirus" on)
option(${CMAKE_PROJECT_NAME}_SYNTH_OSTIRUS "Build OsTIrus" on)
option(${CMAKE_PROJECT_NAME}_SYNTH_VAVRA "Build Vavra" on)
option(${CMAKE_PROJECT_NAME}_SYNTH_XENIA "Build Xenia" on)
option(${CMAKE_PROJECT_NAME}_SYNTH_NODALRED2X "Build NodalRed2x" on)
option(${CMAKE_PROJECT_NAME}_SYNTH_JE8086 "Build JE-8086" on)

# ----------------- Add our cmake scripts to IDE

add_custom_target(cmakeScripts SOURCES
	cmake/base.cmake
	cmake/changelog.cmake
	cmake/exporttarget.cmake
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
add_subdirectory(3rdparty/libresample)

add_subdirectory(3rdparty)

# Lua is intentionally compiled as C++. Explicitly select the language for
# its .c files without modifying the vendor build or relying on Clang's
# deprecated extension inference. MSVC-style drivers already receive /TP.
if(NOT MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
	target_compile_options(lua PRIVATE -x c++)
endif()

include(cmake/macsetup.cmake)
include(cmake/skins.cmake)
include(cmake/exporttarget.cmake)
include(cmake/changelog.cmake)

# ----------------- network bridge

add_subdirectory(3rdparty/ptypes EXCLUDE_FROM_ALL)
if(APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
	# Legacy ptypes uses sprintf, deprecated by the macOS SDK. Keep the
	# vendor workaround private to this target without editing its sources.
	target_compile_options(ptypes PRIVATE -Wno-deprecated-declarations)
endif()
add_subdirectory(framework/networkLib EXCLUDE_FROM_ALL)
add_subdirectory(framework/tools/bridge)

# ----------------- MCP server

# MCP server is always built, but can be enabled/disabled at runtime via plugin settings

# ----------------- Try to install VST2 SDK

include(cmake/findvst2.cmake)

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
	include(cmake/juce.cmake)
endif()

# ----------------- dependencies

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

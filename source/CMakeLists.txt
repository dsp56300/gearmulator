cmake_minimum_required(VERSION 3.15)

option(${CMAKE_PROJECT_NAME}_SYNTH_OSIRUS "Build Osirus" on)
option(${CMAKE_PROJECT_NAME}_SYNTH_OSTIRUS "Build OsTIrus" on)
option(${CMAKE_PROJECT_NAME}_SYNTH_VAVRA "Build Vavra" on)
option(${CMAKE_PROJECT_NAME}_SYNTH_XENIA "Build Xenia" on)
option(${CMAKE_PROJECT_NAME}_SYNTH_NODALRED2X "Build NodalRed2x" on)

# ----------------- Add our cmake scripts to IDE

add_custom_target(cmakeScripts SOURCES findvst2.cmake juce.cmake macsetup.cmake macsetup.command.in runAuValidation.cmake ../base.cmake)

# ----------------- DSP56300 emulator

set(ASMJIT_STATIC TRUE)
set(ASMJIT_NO_INSTALL TRUE)
set(BUILD_SHARED_LIBS OFF)

add_subdirectory(dsp56300/source)

# ----------------- Common libraries used by all synths

add_subdirectory(baseLib)
add_subdirectory(synthLib)
add_subdirectory(libresample)

include(macsetup.cmake)

# ----------------- Try to install VST2 SDK

include(findvst2.cmake)

# ----------------- Juce based audio plugin dependencies

set_property(GLOBAL PROPERTY USE_FOLDERS YES)
if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN)
	set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON CACHE BOOL "" FORCE)
	add_subdirectory(JUCE EXCLUDE_FROM_ALL)
	if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN_CLAP)
		add_subdirectory(clap-juce-extensions)
	endif()
	add_subdirectory(jucePluginLib EXCLUDE_FROM_ALL)
	add_subdirectory(juceUiLib EXCLUDE_FROM_ALL)
	add_subdirectory(jucePluginEditorLib EXCLUDE_FROM_ALL)
	add_subdirectory(jucePluginData EXCLUDE_FROM_ALL)
	
	include(juce.cmake)
endif()

# ----------------- dependencies

add_subdirectory(mc68k EXCLUDE_FROM_ALL)
add_subdirectory(hardwareLib EXCLUDE_FROM_ALL)
add_subdirectory(wLib EXCLUDE_FROM_ALL)

# ----------------- Synths Osirus/OsTIrus

if(${CMAKE_PROJECT_NAME}_SYNTH_OSIRUS OR ${CMAKE_PROJECT_NAME}_SYNTH_OSTIRUS)
	add_subdirectory(virusLib EXCLUDE_FROM_ALL)
	add_subdirectory(virusConsoleLib EXCLUDE_FROM_ALL)
	add_subdirectory(virusTestConsole)
	add_subdirectory(virusIntegrationTest)
	if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN)
		add_subdirectory(virusJucePlugin EXCLUDE_FROM_ALL)
		if(${CMAKE_PROJECT_NAME}_SYNTH_OSIRUS)
			add_subdirectory(osirusJucePlugin)
		endif()
		if(${CMAKE_PROJECT_NAME}_SYNTH_OSTIRUS)
			add_subdirectory(osTIrusJucePlugin)
		endif()
	endif()
endif()

# ----------------- Synth Vavra

if(${CMAKE_PROJECT_NAME}_SYNTH_VAVRA)
	add_subdirectory(mqLib EXCLUDE_FROM_ALL)

	# needed for test console
	set(CPPTERMINAL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(CPPTERMINAL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
	set(CPPTERMINAL_ENABLE_TESING OFF CACHE BOOL "" FORCE)
	add_subdirectory(cpp-terminal EXCLUDE_FROM_ALL)

	if(NOT ANDROID)
		add_subdirectory(portmidi EXCLUDE_FROM_ALL)
	endif()

	set(PA_USE_ASIO OFF CACHE BOOL "" FORCE)
	add_subdirectory(portaudio EXCLUDE_FROM_ALL)

	add_subdirectory(mqConsoleLib EXCLUDE_FROM_ALL)
	add_subdirectory(mqTestConsole)
	add_subdirectory(mqPerformanceTest)

	if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN)
		add_subdirectory(mqJucePlugin)
	endif()
endif()

# ----------------- Synth Xenia

if(${CMAKE_PROJECT_NAME}_SYNTH_XENIA)
	add_subdirectory(xtLib EXCLUDE_FROM_ALL)
	add_subdirectory(xtTestConsole)

	if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN)
		add_subdirectory(xtJucePlugin)
	endif()
endif()

# ----------------- all nords

add_subdirectory(nord)

cmake_minimum_required(VERSION 3.15)
project(osTIrusJucePlugin VERSION ${CMAKE_PROJECT_VERSION}) 

set(SOURCES
	parameterDescriptions_TI.json
	PluginEditorState.cpp
	PluginEditorState.h
	PluginProcessor.cpp
	PluginProcessor.h
	version.h
)

SET(ASSETS "parameterDescriptions_TI.json")

include(skins/TrancyTI/assets.cmake)

juce_add_binary_data(osTIrusJucePlugin_BinaryData SOURCES ${ASSETS} ${ASSETS_VirusTI_Trancy})

createJucePluginWithFX(osTIrusJucePlugin "OsTIrus" "Ttip" "Ttif" osTIrusJucePlugin_BinaryData virusJucePlugin)

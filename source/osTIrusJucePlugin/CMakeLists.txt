cmake_minimum_required(VERSION 3.15)
project(osTIrusJucePlugin VERSION ${CMAKE_PROJECT_VERSION}) 

set(SOURCES
	skins/TrancyTI/VirusTI_Trancy.json
	parameterDescriptions_TI.json
	OsTIrusEditorState.cpp
	OsTIrusEditorState.h
	OsTIrusProcessor.cpp
	OsTIrusProcessor.h
)

SET(ASSETS "parameterDescriptions_TI.json")

include(skins/TrancyTI/assets.cmake)

juce_add_binary_data(osTIrusJucePlugin_BinaryData SOURCES ${ASSETS} ${ASSETS_VirusTI_Trancy})

createJucePluginWithFX(osTIrusJucePlugin "OsTIrus" "Ttip" "Ttif" osTIrusJucePlugin_BinaryData virusJucePlugin)

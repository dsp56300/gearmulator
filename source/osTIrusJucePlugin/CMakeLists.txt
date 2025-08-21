cmake_minimum_required(VERSION 3.15)
project(osTIrusJucePlugin VERSION ${CMAKE_PROJECT_VERSION}) 

set(SOURCES
	skins/TrancyTI/VirusTI_Trancy.json
	skins/TrancyTI/VirusTI_Trancy.rml
	skins/TrancyTI/VirusTI_Trancy.rcss
	parameterDescriptions_TI.json
	OsTIrusEditorState.cpp
	OsTIrusEditorState.h
	OsTIrusProcessor.cpp
	OsTIrusProcessor.h
)

SET(ASSETS "parameterDescriptions_TI.json")

addSkin("OsTIrus" "TrancyTI" "skins/TrancyTI" "VirusTI_Trancy.rml")

buildSkinHeader()

juce_add_binary_data(osTIrusJucePlugin_BinaryData SOURCES ${ASSETS} ${ASSETS_SKINS})

createJucePluginWithFX(osTIrusJucePlugin "OsTIrus" "Ttip" "Ttif" osTIrusJucePlugin_BinaryData virusJucePlugin)

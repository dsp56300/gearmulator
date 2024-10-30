cmake_minimum_required(VERSION 3.15)
project(osirusJucePlugin VERSION ${CMAKE_PROJECT_VERSION}) 

set(SOURCES
	parameterDescriptions_C.json
	OsirusEditorState.cpp
	OsirusEditorState.h
	OsirusProcessor.cpp
	OsirusProcessor.h
)

SET(ASSETS "parameterDescriptions_C.json")

addSkin("Osirus" "Hoverland" "skins/Hoverland" "VirusC_Hoverland.json" "ASSETS_VirusC_Hoverland")
addSkin("Osirus" "Galaxpel" "skins/Galaxpel" "VirusC_Galaxpel.json" "ASSETS_VirusC_Galaxpel")
addSkin("Osirus" "Trancy" "skins/Trancy" "VirusC_Trancy.json" "ASSETS_VirusC_Trancy")

buildSkinHeader()

juce_add_binary_data(osirusJucePlugin_BinaryData SOURCES ${ASSETS} ${ASSETS_SKINS})

createJucePluginWithFX(osirusJucePlugin "Osirus" "TusV" "TusF" osirusJucePlugin_BinaryData virusJucePlugin)

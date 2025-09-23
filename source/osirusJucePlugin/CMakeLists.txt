cmake_minimum_required(VERSION 3.15)
project(osirusJucePlugin VERSION ${CMAKE_PROJECT_VERSION}) 

set(SOURCES
	parameterDescriptions_C.json
	OsirusEditorState.cpp
	OsirusEditorState.h
	OsirusProcessor.cpp
	OsirusProcessor.h

	skins/Hoverland/VirusC_Hoverland.rml
	skins/Hoverland/VirusC_Hoverland.rcss
	skins/Galaxpel/VirusC_Galaxpel.rml
	skins/Galaxpel/VirusC_Galaxpel.rcss
	skins/Trancy/VirusC_Trancy.rml
	skins/Trancy/VirusC_Trancy.rcss
)

SET(ASSETS "parameterDescriptions_C.json")

addSkin("Osirus" "Hoverland" "skins/Hoverland" "VirusC_Hoverland.rml")
addSkin("Osirus" "Galaxpel" "skins/Galaxpel" "VirusC_Galaxpel.rml")
addSkin("Osirus" "Trancy" "skins/Trancy" "VirusC_Trancy.rml")

buildSkinHeader()

juce_add_binary_data(osirusJucePlugin_BinaryData SOURCES ${ASSETS} ${ASSETS_SKINS})

createJucePluginWithFX(osirusJucePlugin "Osirus" "TusV" "TusF" osirusJucePlugin_BinaryData virusJucePlugin)

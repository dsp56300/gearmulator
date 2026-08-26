cmake_minimum_required(VERSION 3.15)
project(osirusJucePlugin VERSION ${CMAKE_PROJECT_VERSION}) 

set(SOURCES
	parameterDescriptions_C.json
	OsirusEditorState.cpp
	OsirusEditorState.h
	OsirusProcessor.cpp
	OsirusProcessor.h

	skins/Galaxpel/VirusC_Galaxpel.rml
	skins/Galaxpel/VirusC_Galaxpel.rcss
)

SET(ASSETS "parameterDescriptions_C.json")

# Hoverland and Trancy skins are kept on disk but no longer bundled in the
# build; Galaxpel is the only shipped skin going forward.
addSkin("Osirus" "Galaxpel" "skins/Galaxpel" "VirusC_Galaxpel.rml")

buildSkinHeader()

juce_add_binary_data(osirusJucePlugin_BinaryData SOURCES ${ASSETS} ${ASSETS_SKINS})

createJucePluginWithFX(osirusJucePlugin "Osirus" "TusV" "TusF" osirusJucePlugin_BinaryData virusJucePlugin)

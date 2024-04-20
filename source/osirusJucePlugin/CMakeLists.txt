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

include(skins/Galaxpel/assets.cmake)
include(skins/Hoverland/assets.cmake)
include(skins/Trancy/assets.cmake)

juce_add_binary_data(osirusJucePlugin_BinaryData SOURCES ${ASSETS} ${ASSETS_VirusC_Galaxpel} ${ASSETS_VirusC_Hoverland} ${ASSETS_VirusC_Trancy})

createJucePluginWithFX(osirusJucePlugin "Osirus" "TusV" "TusF" osirusJucePlugin_BinaryData virusJucePlugin)

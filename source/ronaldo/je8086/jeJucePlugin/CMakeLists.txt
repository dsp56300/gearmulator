cmake_minimum_required(VERSION 3.15)

project(jeJucePlugin VERSION ${CMAKE_PROJECT_VERSION}) 

set(SOURCES
	jeController.cpp jeController.h
	jeEditor.cpp jeEditor.h
	jePluginEditorState.cpp jePluginEditorState.h
	jePluginProcessor.cpp jePluginProcessor.h
	parameterDescriptions_je.json

	skins/jeTrancy/jeTrancy.rml
	skins/jeTrancy/jeTrancy.rcss
)

SET(ASSETS "parameterDescriptions_je.json")

addSkin("JE8086" "jeTrancy" "skins/jeTrancy" "jeTrancy.rml")

buildSkinHeader()

juce_add_binary_data(jeJucePlugin_BinaryData SOURCES ${ASSETS} ${ASSETS_SKINS})

createJucePlugin(jeJucePlugin "JE8086" TRUE "Tjpv" jeJucePlugin_BinaryData jeLib)

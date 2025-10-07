cmake_minimum_required(VERSION 3.15)

project(jeJucePlugin VERSION ${CMAKE_PROJECT_VERSION}) 

set(SOURCES
	jeAssign.cpp jeAssign.h
	jeController.cpp jeController.h
	jeEditor.cpp jeEditor.h
	jeFocusedParameter.cpp jeFocusedParameter.h
	jeLcd.cpp jeLcd.h
	jePartSelect.cpp jePartSelect.h
	jePatchManager.cpp jePatchManager.h
	jePluginEditorState.cpp jePluginEditorState.h
	jePluginProcessor.cpp jePluginProcessor.h
	jeTypes.h
	parameterDescriptions_je.json

	skins/jeTrancy/jeTrancy.rml
	skins/jeTrancy/jeTrancy.rcss
)

SET(ASSETS "parameterDescriptions_je.json")

addSkin("JE8086" "jeTrancy" "skins/jeTrancy" "jeTrancy.rml")

buildSkinHeader()

juce_add_binary_data(jeJucePlugin_BinaryData SOURCES ${ASSETS} ${ASSETS_SKINS})

createJucePlugin(jeJucePlugin "JE8086" TRUE "Tjpv" jeJucePlugin_BinaryData jeLib)

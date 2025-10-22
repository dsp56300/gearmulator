cmake_minimum_required(VERSION 3.15)
project(jucePlugin VERSION ${CMAKE_PROJECT_VERSION}) 

set(SOURCES
	parameterDescriptions_mq.json

    PluginEditorState.cpp PluginEditorState.h
    PluginProcessor.cpp PluginProcessor.h
	mqController.cpp mqController.h
	mqEditor.cpp mqEditor.h
	mqFrontPanel.cpp mqFrontPanel.h
	mqLcd.cpp mqLcd.h
	mqLcdBase.cpp mqLcdBase.h
	mqLcdText.cpp mqLcdText.h
	mqPartButton.cpp mqPartButton.h
	mqPartSelect.cpp mqPartSelect.h
	mqPatchManager.cpp mqPatchManager.h
	mqPatchBrowser.h

	skins/mqDefault/mqDefault.rml
	skins/mqDefault/mqDefault.rcss

	skins/mqFrontPanel/mqFrontPanel.rml
	skins/mqFrontPanel/mqFrontPanel.rcss
)

SET(ASSETS "parameterDescriptions_mq.json")

addSkin("Vavra" "mqDefault" "skins/mqDefault" "mqDefault.rml")
addSkin("Vavra" "mqFrontPanel" "skins/mqFrontPanel" "mqFrontPanel.rml")

buildSkinHeader()

juce_add_binary_data(mqJucePlugin_BinaryData SOURCES ${ASSETS} ${ASSETS_SKINS})

createJucePluginWithFX(mqJucePlugin "Vavra" "Tmqs" "Tmqf" mqJucePlugin_BinaryData mqLib)

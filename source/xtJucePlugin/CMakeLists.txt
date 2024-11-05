cmake_minimum_required(VERSION 3.15)
project(xtJucePlugin VERSION ${CMAKE_PROJECT_VERSION}) 

set(SOURCES
	parameterDescriptions_xt.json
	skins/xtDefault/xtDefault.json

	PluginEditorState.cpp PluginEditorState.h
	PluginProcessor.cpp PluginProcessor.h
	weControlTree.cpp weControlTree.h
	weControlTreeItem.cpp weControlTreeItem.h
	weData.cpp weData.h
	weGraph.cpp weGraph.h
	weGraphData.cpp weGraphData.h
	weGraphFreq.cpp weGraphFreq.h
	weGraphPhase.cpp weGraphPhase.h
	weGraphTime.cpp weGraphTime.h
	weTablesTree.cpp weTablesTree.h
	weTablesTreeItem.cpp weTablesTreeItem.h
	weTree.cpp weTree.h
	weTreeItem.cpp weTreeItem.h
	weTypes.h
	weWaveCategoryTreeItem.cpp weWaveCategoryTreeItem.h
	weWaveDesc.cpp weWaveDesc.h
	weWaveform.cpp weWaveform.h
	weWaveTree.cpp weWaveTree.h
	weWaveTreeItem.cpp weWaveTreeItem.h
	xtArp.cpp xtArp.h
	xtController.cpp xtController.h
	xtEditor.cpp xtEditor.h
	xtFrontPanel.cpp xtFrontPanel.h
	xtFocusedParameter.cpp xtFocusedParameter.h
	xtLcd.cpp xtLcd.h
	xtPartButton.cpp xtPartButton.h
	xtPartName.cpp xtPartName.h
	xtParts.cpp xtParts.h
	xtPatchManager.cpp xtPatchManager.h
	xtWaveEditor.cpp xtWaveEditor.h
	xtWaveEditorStyle.cpp xtWaveEditorStyle.h
)

SET(ASSETS "parameterDescriptions_xt.json")

addSkin("Xenia" "xtDefault" "skins/xtDefault" "xtDefault.json" "ASSETS_xtDefault")

buildSkinHeader()

juce_add_binary_data(xtJucePlugin_BinaryData SOURCES ${ASSETS} ${ASSETS_xtDefault})

createJucePluginWithFX(xtJucePlugin "Xenia" "Txts" "Txtf" xtJucePlugin_BinaryData xtLib)

target_link_libraries(xtJucePlugin PUBLIC juce::juce_dsp)

if(${CMAKE_PROJECT_NAME}_BUILD_FX_PLUGIN)
	target_link_libraries(xtJucePlugin_FX PUBLIC juce::juce_dsp)
endif()

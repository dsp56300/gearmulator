cmake_minimum_required(VERSION 3.15)
project(xtJucePlugin VERSION ${CMAKE_PROJECT_VERSION}) 

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.h.in ${CMAKE_CURRENT_SOURCE_DIR}/version.h)

set(SOURCES
	parameterDescriptions_xt.json
	PluginEditor.cpp PluginEditor.h
	PluginEditorState.cpp PluginEditorState.h
	PluginProcessor.cpp PluginProcessor.h
	xtController.cpp xtController.h
	xtEditor.cpp xtEditor.h
	xtFrontPanel.cpp xtFrontPanel.h
	xtFocusedParameter.cpp xtFocusedParameter.h
	xtLcd.cpp xtLcd.h
	xtPatchManager.cpp xtPatchManager.h
	version.h
)

# https://forum.juce.com/t/help-needed-using-binarydata-with-cmake-juce-6/40486
# "This might be because the BinaryData files are generated during the build, so the IDE may not be able to find them until the build has been run once (and even then, some IDEs might need a bit of a nudge to re-index the binary directory…)"
SET(ASSETS "parameterDescriptions_xt.json")

#include(skins/xtFrontPanel/assets.cmake)
include(skins/xtDefault/assets.cmake)

juce_add_binary_data(xtJucePlugin_BinaryData SOURCES ${ASSETS} ${ASSETS_xtDefault})

createJucePluginWithFX(xtJucePlugin "Xenia" "Txts" "Txtf" xtJucePlugin_BinaryData xtLib)

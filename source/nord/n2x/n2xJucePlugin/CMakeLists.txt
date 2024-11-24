cmake_minimum_required(VERSION 3.15)

project(n2xJucePlugin VERSION ${CMAKE_PROJECT_VERSION}) 

set(SOURCES
	n2xArp.cpp n2xArp.h
	n2xController.cpp n2xController.h
	n2xEditor.cpp n2xEditor.h
	n2xFocusedParameter.cpp n2xFocusedParameter.h
	n2xMasterVolume.cpp n2xMasterVolume.h
	n2xOctLed.cpp n2xOctLed.h
	n2xOutputMode.cpp n2xOutputMode.h
	n2xParameterDrivenLed.cpp n2xParameterDrivenLed.h
	n2xLcd.cpp n2xLcd.h
	n2xLfo.cpp n2xLfo.h
	n2xPart.cpp n2xPart.h
	n2xPartLed.cpp n2xPartLed.h
	n2xParts.cpp n2xParts.h
	n2xPatchManager.cpp n2xPatchManager.h
	n2xPluginEditorState.cpp n2xPluginEditorState.h
	n2xPluginProcessor.cpp n2xPluginProcessor.h
	n2xSlider.cpp n2xSlider.h
	n2xVmMap.cpp n2xVmMap.h
	parameterDescriptions_n2x.json
	skins/n2xTrancy/n2xTrancy.json
)

SET(ASSETS "parameterDescriptions_n2x.json")

addSkin("NodalRed2x" "n2xTrancy" "skins/n2xTrancy" "n2xTrancy.json" "ASSETS_n2xTrancy")

buildSkinHeader()

juce_add_binary_data(n2xJucePlugin_BinaryData SOURCES ${ASSETS} ${ASSETS_SKINS})

createJucePlugin(n2xJucePlugin "NodalRed2x" TRUE "Tn2x" n2xJucePlugin_BinaryData n2xLib)

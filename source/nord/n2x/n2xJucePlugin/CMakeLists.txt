cmake_minimum_required(VERSION 3.15)

project(n2xJucePlugin VERSION ${CMAKE_PROJECT_VERSION}) 

set(SOURCES
	n2xController.cpp n2xController.h
	n2xPatchManager.cpp n2xPatchManager.h
	n2xPluginProcessor.cpp n2xPluginProcessor.h
	parameterDescriptions_n2x.json
)

# https://forum.juce.com/t/help-needed-using-binarydata-with-cmake-juce-6/40486
# "This might be because the BinaryData files are generated during the build, so the IDE may not be able to find them until the build has been run once (and even then, some IDEs might need a bit of a nudge to re-index the binary directory…)"
SET(ASSETS "parameterDescriptions_n2x.json")

juce_add_binary_data(n2xJucePlugin_BinaryData SOURCES ${ASSETS})

createJucePlugin(n2xJucePlugin "NodalRed" TRUE "Tn2x" n2xJucePlugin_BinaryData n2xLib)

cmake_minimum_required(VERSION 3.15)
project(jucePlugin VERSION ${CMAKE_PROJECT_VERSION}) 

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.h.in ${CMAKE_CURRENT_SOURCE_DIR}/version.h)

set(SOURCES
	parameterDescriptions_C.json
	ParameterNames.h
	PluginEditorState.cpp
	PluginEditorState.h
    PluginProcessor.cpp
    PluginProcessor.h
	VirusController.cpp
	VirusController.h
	version.h

	ui3/ControllerLinks.cpp
	ui3/ControllerLinks.h
	ui3/FxPage.cpp
	ui3/FxPage.h
    ui3/Parts.cpp
    ui3/Parts.h
	ui3/PatchBrowser.cpp
	ui3/PatchBrowser.h
	ui3/PatchManager.cpp
	ui3/PatchManager.h
    ui3/Tabs.cpp
    ui3/Tabs.h
    ui3/VirusEditor.cpp
    ui3/VirusEditor.h
)

# https://forum.juce.com/t/help-needed-using-binarydata-with-cmake-juce-6/40486
# "This might be because the BinaryData files are generated during the build, so the IDE may not be able to find them until the build has been run once (and even then, some IDEs might need a bit of a nudge to re-index the binary directory…)"
SET(ASSETS "parameterDescriptions_C.json")

include(skins/Galaxpel/assets.cmake)
include(skins/Hoverland/assets.cmake)
include(skins/Trancy/assets.cmake)

juce_add_binary_data(jucePlugin_BinaryData SOURCES ${ASSETS} ${ASSETS_VirusC_Galaxpel} ${ASSETS_VirusC_Hoverland} ${ASSETS_VirusC_Trancy})

createJucePluginWithFX(jucePlugin "Osirus" "TusV" "TusF" jucePlugin_BinaryData virusLib)

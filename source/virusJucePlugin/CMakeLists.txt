cmake_minimum_required(VERSION 3.15)
project(virusJucePlugin VERSION ${CMAKE_PROJECT_VERSION}) 

set(SOURCES
	ArpUserPattern.cpp
	ArpUserPattern.h
	ControllerLinks.cpp
	ControllerLinks.h
	FxPage.cpp
	FxPage.h
	Leds.cpp
	Leds.h
	ParameterNames.h
	PartButton.cpp
	PartButton.h
	Parts.cpp
	Parts.h
	PatchManager.cpp
	PatchManager.h
	Tabs.cpp
	Tabs.h
	VirusController.cpp
	VirusController.h
	VirusEditor.cpp
	VirusEditor.h
	VirusEditorState.cpp
	VirusEditorState.h
	VirusProcessor.cpp
	VirusProcessor.h
)

add_library(virusJucePlugin STATIC)

target_sources(virusJucePlugin PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(virusJucePlugin PUBLIC jucePluginEditorLib virusLib)

target_include_directories(virusJucePlugin PUBLIC ../JUCE/modules)

target_compile_definitions(virusJucePlugin PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_compile_definitions(virusJucePlugin PUBLIC JUCE_MODAL_LOOPS_PERMITTED=1)

set_property(TARGET virusJucePlugin PROPERTY FOLDER "Virus")

target_include_directories(virusJucePlugin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

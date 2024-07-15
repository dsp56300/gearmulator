cmake_minimum_required(VERSION 3.15)
project(juceUiLib VERSION ${CMAKE_PROJECT_VERSION}) 

set(SOURCES
	button.cpp button.h
	condition.cpp condition.h
	controllerlink.cpp controllerlink.h
	editor.cpp editor.h
	editorInterface.h
	image.cpp image.h
	listBoxStyle.cpp listBoxStyle.h
	rotaryStyle.cpp rotaryStyle.h
	comboboxStyle.cpp comboboxStyle.h
	buttonStyle.cpp buttonStyle.h
	labelStyle.cpp labelStyle.h
	scrollbarStyle.cpp scrollbarStyle.h
	slider.cpp slider.h
	textbuttonStyle.cpp textbuttonStyle.h
	textEditorStyle.cpp textEditorStyle.h
	treeViewStyle.cpp treeViewStyle.h
	hyperlinkbuttonStyle.cpp hyperlinkbuttonStyle.h
	tabgroup.cpp tabgroup.h
	uiObject.cpp uiObject.h
	uiObjectStyle.cpp uiObjectStyle.h
)

add_library(juceUiLib STATIC)

target_sources(juceUiLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(juceUiLib PUBLIC synthLib)

target_include_directories(juceUiLib PUBLIC ../JUCE/modules)
target_compile_definitions(juceUiLib PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_compile_definitions(juceUiLib PUBLIC JUCE_MODAL_LOOPS_PERMITTED=1)
set_property(TARGET juceUiLib PROPERTY FOLDER "Gearmulator")

target_include_directories(juceUiLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

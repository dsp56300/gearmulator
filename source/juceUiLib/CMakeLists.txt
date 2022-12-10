cmake_minimum_required(VERSION 3.15)
project(juceUiLib VERSION ${CMAKE_PROJECT_VERSION}) 

set(SOURCES
	editor.cpp editor.h
	editorInterface.h
	condition.cpp condition.h
	controllerlink.cpp controllerlink.h
	image.cpp image.h
	rotaryStyle.cpp rotaryStyle.h
	comboboxStyle.cpp comboboxStyle.h
	buttonStyle.cpp buttonStyle.h
	labelStyle.cpp labelStyle.h
	textbuttonStyle.cpp textbuttonStyle.h
	hyperlinkbuttonStyle.cpp hyperlinkbuttonStyle.h
	tabgroup.cpp tabgroup.h
	uiObject.cpp uiObject.h
	uiObjectStyle.cpp uiObjectStyle.h
)

add_library(juceUiLib STATIC)

target_sources(juceUiLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_include_directories(juceUiLib PUBLIC ../JUCE/modules)
target_compile_definitions(juceUiLib PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)

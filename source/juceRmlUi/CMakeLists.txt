cmake_minimum_required(VERSION 3.15)
project(juceRmlUi VERSION ${CMAKE_PROJECT_VERSION}) 

set(SOURCES
	juceRmlUi.cpp juceRmlUi.h
	juceRmlComponent.cpp juceRmlComponent.h
	rmlDataProvider.h
	rmlDocuments.h
	rmlElemComboBox.cpp rmlElemComboBox.h
	rmlElement.cpp rmlElement.h
	rmlElemKnob.cpp rmlElemKnob.h
	rmlElemSlider.cpp rmlElemSlider.h
	rmlElemValue.cpp rmlElemValue.h
	rmlEventListener.cpp rmlEventListener.h
	rmlFileInterface.cpp rmlFileInterface.h
	rmlHelper.cpp rmlHelper.h
	rmlInterfaces.cpp rmlInterfaces.h
	rmlMenu.cpp rmlMenu.h
	rmlRenderInterface.cpp rmlRenderInterface.h
	rmlSystemInterface.cpp rmlSystemInterface.h
)

add_library(juceRmlUi STATIC)

target_sources(juceRmlUi PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(juceRmlUi PUBLIC baseLib rmlui_core)

target_include_directories(juceRmlUi PUBLIC ../JUCE/modules)
target_compile_definitions(juceRmlUi PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
set_property(TARGET juceRmlUi PROPERTY FOLDER "Gearmulator")

target_include_directories(juceRmlUi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

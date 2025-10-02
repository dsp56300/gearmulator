cmake_minimum_required(VERSION 3.15)
project(juceRmlPlugin VERSION ${CMAKE_PROJECT_VERSION}) 

set(SOURCES
	rmlControllerLink.cpp rmlControllerLink.h
	rmlPlugin.cpp rmlPlugin.h
	rmlPluginContext.cpp rmlPluginContext.h
	rmlPluginDocument.cpp rmlPluginDocument.h
	rmlParameterBinding.cpp rmlParameterBinding.h
	rmlParameterRef.cpp rmlParameterRef.h
	rmlParameterToElementsBinding.cpp rmlParameterToElementsBinding.h
	rmlTabGroup.cpp rmlTabGroup.h
)

set(SOURCES_SC
	skinConverter/coAttribs.cpp skinConverter/coAttribs.h
	skinConverter/convertedObject.cpp skinConverter/convertedObject.h
	skinConverter/coPosition.cpp skinConverter/coPosition.h
	skinConverter/coStyle.cpp skinConverter/coStyle.h
	skinConverter/rmlWriter.cpp skinConverter/rmlWriter.h
	skinConverter/scHelper.cpp skinConverter/scHelper.h
	skinConverter/skinConverter.cpp skinConverter/skinConverter.h
)

add_library(juceRmlPlugin STATIC)

target_sources(juceRmlPlugin PRIVATE ${SOURCES} ${SOURCES_SC})
source_group("source" FILES ${SOURCES})
source_group("source/skinConverter" FILES ${SOURCES_SC})

target_link_libraries(juceRmlPlugin PUBLIC juceRmlUi jucePluginLib juceUiLib)

target_include_directories(juceRmlPlugin PUBLIC ../JUCE/modules)
target_compile_definitions(juceRmlPlugin PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
set_property(TARGET juceRmlPlugin PROPERTY FOLDER "Gearmulator")

target_include_directories(juceRmlPlugin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

####################

if(MSVC)
	add_executable(knobConverter)

	set(SOURCES
		knobConverter.cpp knobConverter.h
	)

	target_sources(knobConverter PUBLIC ${SOURCES})
	source_group("source" FILES ${SOURCES})

	target_link_libraries(knobConverter PRIVATE rmlui_core baseLib juceRmlPlugin juce_plugin_modules)
	target_include_directories(knobConverter PUBLIC ../JUCE/modules)
	target_compile_definitions(knobConverter PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
	set_property(TARGET knobConverter PROPERTY FOLDER "Gearmulator")
endif()

cmake_minimum_required(VERSION 3.15)
project(jucePluginLib VERSION ${CMAKE_PROJECT_VERSION}) 

set(SOURCES
	event.cpp event.h
	controller.cpp controller.h
	dummydevice.cpp dummydevice.h
	midipacket.cpp midipacket.h
	parameter.cpp parameter.h
	parameterbinding.cpp parameterbinding.h
	parameterdescription.cpp parameterdescription.h
	parameterdescriptions.cpp parameterdescriptions.h
	parameterlink.cpp parameterlink.h
	processor.cpp processor.h
)

set(SOURCES_PATCHDB
	patchdb/datasource.cpp patchdb/datasource.h
	patchdb/db.cpp patchdb/db.h
	patchdb/jobqueue.cpp patchdb/jobqueue.h
	patchdb/patch.cpp patchdb/patch.h
	patchdb/patchdbtypes.cpp patchdb/patchdbtypes.h
	patchdb/patchhistory.cpp patchdb/patchhistory.h
	patchdb/patchmodifications.cpp patchdb/patchmodifications.h
	patchdb/search.cpp patchdb/search.h
	patchdb/state.cpp patchdb/state.h
	patchdb/serialization.cpp patchdb/serialization.h
	patchdb/tags.cpp patchdb/tags.h
)

add_library(jucePluginLib STATIC)

target_sources(jucePluginLib PRIVATE ${SOURCES} ${SOURCES_PATCHDB})
source_group("source" FILES ${SOURCES})
source_group("source\\patchdb" FILES ${SOURCES_PATCHDB})

target_link_libraries(jucePluginLib PUBLIC juceUiLib synthLib)
target_include_directories(jucePluginLib PUBLIC ../JUCE/modules)
target_compile_definitions(jucePluginLib PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)

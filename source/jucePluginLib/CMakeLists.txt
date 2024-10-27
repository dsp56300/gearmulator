cmake_minimum_required(VERSION 3.15)
project(jucePluginLib VERSION ${CMAKE_PROJECT_VERSION}) 

configure_file(${CMAKE_CURRENT_LIST_DIR}/version.h.in ${CMAKE_CURRENT_LIST_DIR}/version.h)

file(TOUCH_NOCREATE ${CMAKE_CURRENT_LIST_DIR}/version.h.in)
file(TOUCH_NOCREATE ${CMAKE_CURRENT_LIST_DIR}/versionDateTime.h.in)

set(SOURCES
	bypassBuffer.cpp bypassBuffer.h
	createVersionDateTime.cmake
	clipboard.cpp clipboard.h
	controller.cpp controller.h
	controllermap.cpp controllermap.h
	dummydevice.cpp dummydevice.h
	event.cpp event.h
	midipacket.cpp midipacket.h
	midiports.cpp midiports.h
	parameter.cpp parameter.h
	parameterbinding.cpp parameterbinding.h
	parameterdescription.cpp parameterdescription.h
	parameterdescriptions.cpp parameterdescriptions.h
	parameterlink.cpp parameterlink.h
	parameterlinks.cpp parameterlinks.h
	parameterlistener.cpp parameterlistener.h
	parameterlocking.cpp parameterlocking.h
	parameterregion.cpp parameterregion.h
	parametervaluelist.cpp parametervaluelist.h
	pluginVersion.cpp pluginVersion.h
	processor.cpp processor.h
	softknob.cpp softknob.h
	tools.cpp tools.h
	types.h
	version.h.in version.h
	versionDateTime.h.in versionDateTime.h
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
set_property(TARGET jucePluginLib PROPERTY FOLDER "Gearmulator")

add_custom_command(
	PRE_BUILD
	OUTPUT ${CMAKE_CURRENT_LIST_DIR}/_doesnotexist_versionDateTime.h	# fake, causes regeneration for every build, exactly what we want
	OUTPUT ${CMAKE_CURRENT_LIST_DIR}/versionDateTime.h	# valid
	DEPENDS ${CMAKE_CURRENT_LIST_DIR}/versionDateTime.h.in
	COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_LIST_DIR}/createVersionDateTime.cmake"
)

target_include_directories(jucePluginLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

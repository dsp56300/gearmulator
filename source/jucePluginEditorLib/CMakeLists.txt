cmake_minimum_required(VERSION 3.15)
project(jucePluginEditorLib VERSION ${CMAKE_PROJECT_VERSION}) 

set(SOURCES
	focusedParameter.cpp focusedParameter.h
	focusedParameterTooltip.cpp focusedParameterTooltip.h
	midiPorts.cpp midiPorts.h
	partbutton.cpp partbutton.h
	pluginEditor.cpp pluginEditor.h
	pluginEditorWindow.cpp pluginEditorWindow.h
	pluginEditorState.cpp pluginEditorState.h
	pluginProcessor.cpp pluginProcessor.h
	types.h
)

set(SOURCES_PM
	patchmanager/datasourcetree.cpp patchmanager/datasourcetree.h
	patchmanager/datasourcetreeitem.cpp patchmanager/datasourcetreeitem.h
	patchmanager/defaultskin.h
	patchmanager/editable.cpp patchmanager/editable.h
	patchmanager/grouptreeitem.cpp patchmanager/grouptreeitem.h
	patchmanager/info.cpp patchmanager/info.h
	patchmanager/list.cpp patchmanager/list.h
	patchmanager/listitem.cpp patchmanager/listitem.h
	patchmanager/notagtreeitem.cpp patchmanager/notagtreeitem.h
	patchmanager/patchmanager.cpp patchmanager/patchmanager.h
	patchmanager/resizerbar.cpp patchmanager/resizerbar.h
	patchmanager/roottreeitem.cpp patchmanager/roottreeitem.h
	patchmanager/savepatchdesc.cpp patchmanager/savepatchdesc.h
	patchmanager/tagtreeitem.cpp patchmanager/tagtreeitem.h
	patchmanager/tagstree.cpp patchmanager/tagstree.h
	patchmanager/tree.cpp patchmanager/tree.h
	patchmanager/treeitem.cpp patchmanager/treeitem.h
	patchmanager/types.cpp patchmanager/types.h
	patchmanager/search.cpp patchmanager/search.h
	patchmanager/searchlist.cpp patchmanager/searchlist.h
	patchmanager/searchtree.cpp patchmanager/searchtree.h
	patchmanager/state.cpp patchmanager/state.h
	patchmanager/status.cpp patchmanager/status.h
)

add_library(jucePluginEditorLib STATIC)

target_sources(jucePluginEditorLib PRIVATE ${SOURCES} ${SOURCES_PM})
source_group("source" FILES ${SOURCES})
source_group("source\\patchmanager" FILES ${SOURCES_PM})

target_link_libraries(jucePluginEditorLib PUBLIC jucePluginLib juceUiLib)
target_include_directories(jucePluginEditorLib PUBLIC ../JUCE/modules)
target_compile_definitions(jucePluginEditorLib PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)

cmake_minimum_required(VERSION 3.15)
project(jucePluginEditorLib VERSION ${CMAKE_PROJECT_VERSION}) 

set(SOURCES
	focusedParameter.cpp focusedParameter.h
	focusedParameterTooltip.cpp focusedParameterTooltip.h
	lcd.cpp lcd.h
	led.cpp led.h
	midiPorts.cpp midiPorts.h
	parameterOverlay.cpp parameterOverlay.h
	parameterOverlays.cpp parameterOverlays.h
	partbutton.cpp partbutton.h
	pluginDataModel.cpp pluginDataModel.h
	pluginEditor.cpp pluginEditor.h
	pluginEditorWindow.cpp pluginEditorWindow.h
	pluginEditorState.cpp pluginEditorState.h
	pluginProcessor.cpp pluginProcessor.h
	skin.h
)

set(SOURCES_PM
	patchmanager/datasourcetree.cpp patchmanager/datasourcetree.h
	patchmanager/datasourcetreeitem.cpp patchmanager/datasourcetreeitem.h
	patchmanager/defaultskin.h
	patchmanager/grid.cpp patchmanager/grid.h
	patchmanager/griditem.cpp patchmanager/griditem.h
	patchmanager/griditemcontainer.cpp patchmanager/griditemcontainer.h
	patchmanager/gridviewport.cpp patchmanager/gridviewport.h
	patchmanager/grouptreeitem.cpp patchmanager/grouptreeitem.h
	patchmanager/list.cpp patchmanager/list.h
	patchmanager/listmodel.cpp patchmanager/listmodel.h
	patchmanager/listitem.cpp patchmanager/listitem.h
	patchmanager/notagtreeitem.cpp patchmanager/notagtreeitem.h
	patchmanager/patchmanager.cpp patchmanager/patchmanager.h
	patchmanager/patchmanagerui.cpp patchmanager/patchmanagerui.h
	patchmanager/patchmanageruijuce.cpp patchmanager/patchmanageruijuce.h
	patchmanager/roottreeitem.cpp patchmanager/roottreeitem.h
	patchmanager/savepatchdesc.cpp patchmanager/savepatchdesc.h
	patchmanager/tagtreeitem.cpp patchmanager/tagtreeitem.h
	patchmanager/tagstree.cpp patchmanager/tagstree.h
	patchmanager/tree.cpp patchmanager/tree.h
	patchmanager/treeitem.cpp patchmanager/treeitem.h
	patchmanager/types.cpp patchmanager/types.h
	patchmanager/state.cpp patchmanager/state.h
)

set(SOURCES_PM_RML
	patchmanagerUiRml/datasourceTree.cpp patchmanagerUiRml/datasourceTree.h
	patchmanagerUiRml/datasourceTreeNode.cpp patchmanagerUiRml/datasourceTreeNode.h
	patchmanagerUiRml/grid.cpp patchmanagerUiRml/grid.h
	patchmanagerUiRml/griditem.cpp patchmanagerUiRml/griditem.h
	patchmanagerUiRml/groupTreeNode.cpp patchmanagerUiRml/groupTreeNode.h
	patchmanagerUiRml/info.cpp patchmanagerUiRml/info.h
	patchmanagerUiRml/list.cpp patchmanagerUiRml/list.h
	patchmanagerUiRml/listmodel.cpp patchmanagerUiRml/listmodel.h
	patchmanagerUiRml/listitem.cpp patchmanagerUiRml/listitem.h
	patchmanagerUiRml/patchmanagerDataModel.cpp patchmanagerUiRml/patchmanagerDataModel.h
	patchmanagerUiRml/patchmanagerUiRml.cpp patchmanagerUiRml/patchmanagerUiRml.h
	patchmanagerUiRml/search.cpp patchmanagerUiRml/search.h
	patchmanagerUiRml/searchlist.cpp patchmanagerUiRml/searchlist.h
	patchmanagerUiRml/searchtree.cpp patchmanagerUiRml/searchtree.h
	patchmanagerUiRml/status.cpp patchmanagerUiRml/status.h
	patchmanagerUiRml/tagsTree.cpp patchmanagerUiRml/tagsTree.h
	patchmanagerUiRml/tagTreeNode.cpp patchmanagerUiRml/tagTreeNode.h
	patchmanagerUiRml/tree.cpp patchmanagerUiRml/tree.h
	patchmanagerUiRml/treeNode.cpp patchmanagerUiRml/treeNode.h
)

add_library(jucePluginEditorLib STATIC)

target_sources(jucePluginEditorLib PRIVATE ${SOURCES} ${SOURCES_PM} ${SOURCES_PM_RML})
source_group("source" FILES ${SOURCES})
source_group("source\\patchmanager" FILES ${SOURCES_PM})
source_group("source\\patchmanagerUiRml" FILES ${SOURCES_PM_RML})

target_link_libraries(jucePluginEditorLib PUBLIC jucePluginLib juceUiLib jucePluginData juceRmlPlugin)

target_include_directories(jucePluginEditorLib PUBLIC ../JUCE/modules)
target_compile_definitions(jucePluginEditorLib PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
set_property(TARGET jucePluginEditorLib PROPERTY FOLDER "Gearmulator")

target_include_directories(jucePluginEditorLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

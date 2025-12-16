cmake_minimum_required(VERSION 3.15)

project(juceRmlUi VERSION ${CMAKE_PROJECT_VERSION}) 

set(SOURCES
	frameRateLimiter.cpp frameRateLimiter.h
	juceRmlComponent.cpp juceRmlComponent.h 
	juceRmlComponentConfig.h
	juceRmlDrag.cpp juceRmlDrag.h
	juceRmlLookAndFeel.cpp juceRmlLookAndFeel.h
	juceRmlUi.cpp juceRmlUi.h
	rmlColorPicker.cpp rmlColorPicker.h
	rmlDataProvider.h
	rmlDocuments.h
	rmlDragData.h
	rmlDragSource.cpp rmlDragSource.h
	rmlDragTarget.cpp rmlDragTarget.h
	rmlElemComboBox.cpp rmlElemComboBox.h
	rmlElement.cpp rmlElement.h
	rmlElemButton.cpp rmlElemButton.h
	rmlElemCanvas.cpp rmlElemCanvas.h
	rmlElemKnob.cpp rmlElemKnob.h
	rmlElemList.cpp rmlElemList.h
	rmlElemListEntry.cpp rmlElemListEntry.h
	rmlElemSplitter.cpp rmlElemSplitter.h
	rmlElemTree.cpp rmlElemTree.h
	rmlElemTreeNode.cpp rmlElemTreeNode.h
	rmlElemValue.cpp rmlElemValue.h
	rmlEventListener.cpp rmlEventListener.h
	rmlFileInterface.cpp rmlFileInterface.h
	rmlHelper.cpp rmlHelper.h
	rmlInplaceEditor.cpp rmlInplaceEditor.h
	rmlInstancers.h
	rmlInterfaces.cpp rmlInterfaces.h
	rmlList.cpp rmlList.h
	rmlListEntry.cpp rmlListEntry.h
	rmlMenu.cpp rmlMenu.h
	rmlRendererProxy.cpp rmlRendererProxy.h
	rmlRendererGL2.cpp rmlRendererGL2.h
	rmlRendererGL2Types.h
	rmlRendererJuce.cpp rmlRendererJuce.h
	rmlRenderInterfaceFilters.cpp rmlRenderInterfaceFilters.h
	rmlRenderInterfaceShaders.cpp rmlRenderInterfaceShaders.h
	rmlShader.cpp rmlShader.h
	rmlShaders.h
	rmlSystemInterface.cpp rmlSystemInterface.h
	rmlTree.cpp rmlTree.h
	rmlTreeNode.cpp rmlTreeNode.h

	RmlUi_Renderer_GL2.cpp RmlUi_Renderer_GL2.h
	RmlUi_Renderer_GL3.cpp RmlUi_Renderer_GL3.h
)

add_library(juceRmlUi STATIC)

target_sources(juceRmlUi PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(juceRmlUi PUBLIC baseLib dsp56kBase rmlui_core rmlui_debugger)

target_include_directories(juceRmlUi PUBLIC ../JUCE/modules)
target_compile_definitions(juceRmlUi PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)

if(APPLE)
	target_compile_definitions(juceRmlUi PRIVATE JUCE_MAC=1)
else()
	target_compile_definitions(juceRmlUi PRIVATE JUCE_MAC=0)
endif()

check_cxx_compiler_flag(-mssse3 HAS_SSSE3)
check_cxx_compiler_flag(-msse4.1 HAS_SSE41)

if(HAS_SSSE3)
    target_compile_options(juceRmlUi PRIVATE -mssse3)
endif()

if(HAS_SSE41)
    target_compile_options(juceRmlUi PRIVATE -msse4.1)
endif()

set_property(TARGET juceRmlUi PROPERTY FOLDER "Gearmulator")

target_compile_definitions(juceRmlUi PUBLIC RMLUI_GL3_CUSTOM_LOADER="juce_opengl/juce_opengl.h")

target_include_directories(juceRmlUi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

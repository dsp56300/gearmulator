cmake_minimum_required(VERSION 3.15)

project(mcpServerLib)

add_library(mcpServerLib STATIC)

set(SOURCES
	discoveryFile.cpp discoveryFile.h
	httpRequest.h
	httpResponse.h
	httpServer.cpp httpServer.h
	jsonHelpers.h
	jsonRpc.h
	mcpPluginServer.cpp mcpPluginServer.h
	mcpServer.cpp mcpServer.h
	mcpTool.h
	mcpTypes.h
)

target_sources(mcpServerLib PRIVATE ${SOURCES})

source_group("source" FILES ${SOURCES})

target_link_libraries(mcpServerLib PUBLIC networkLib jucePluginLib)
target_include_directories(mcpServerLib PUBLIC ../ ../JUCE/modules)
target_compile_definitions(mcpServerLib PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
set_property(TARGET mcpServerLib PROPERTY FOLDER "MCP")

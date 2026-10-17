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
	mcpDomTools.cpp mcpDomTools.h
	mcpServer.cpp mcpServer.h
	mcpTool.h
	mcpTypes.h
)

target_sources(mcpServerLib PRIVATE ${SOURCES})

source_group("source" FILES ${SOURCES})

# The DOM tools drive an RmlUi document, whatever hosts it - a plugin editor or the plugin manager -
# so nothing here depends on the plugin libraries any more.
target_link_libraries(mcpServerLib PUBLIC networkLib juceRmlUi)
target_include_directories(mcpServerLib PUBLIC ../)
target_compile_definitions(mcpServerLib PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
set_property(TARGET mcpServerLib PROPERTY FOLDER "MCP")

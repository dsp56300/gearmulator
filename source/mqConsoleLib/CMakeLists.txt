cmake_minimum_required(VERSION 3.10)
project(mqConsoleLib)

add_library(mqConsoleLib STATIC)

set(SOURCES
	mqGui.cpp mqGui.h
	mqGuiBase.cpp mqGuiBase.h
	mqKeyInput.cpp mqKeyInput.h
)

target_sources(mqConsoleLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(mqConsoleLib PUBLIC mqLib cpp-terminal)

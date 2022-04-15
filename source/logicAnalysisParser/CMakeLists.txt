cmake_minimum_required(VERSION 3.10)

project(logicAnalysisParser)

add_executable(logicAnalysisParser)

set(SOURCES
	logicAnalysisParser.cpp
)

target_sources(logicAnalysisParser PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

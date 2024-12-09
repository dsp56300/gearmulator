cmake_minimum_required(VERSION 3.10)

project(commandlineGenerator)

add_executable(commandlineGenerator)

set(SOURCES
	commandlineGenerator.cpp
)

target_sources(commandlineGenerator PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(commandlineGenerator PUBLIC baseLib)
	
set (outputDir "${CMAKE_SOURCE_DIR}/bin/tools")

set_property(TARGET commandlineGenerator PROPERTY RUNTIME_OUTPUT_DIRECTORY "${outputDir}")

get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
		
if(isMultiConfig)
	set_property(TARGET commandlineGenerator PROPERTY RUNTIME_OUTPUT_DIRECTORY_DEBUG "${outputDir}/Debug")
	set_property(TARGET commandlineGenerator PROPERTY RUNTIME_OUTPUT_DIRECTORY_RELEASE "${outputDir}")
endif()

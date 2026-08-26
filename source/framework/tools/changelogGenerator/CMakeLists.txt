cmake_minimum_required(VERSION 3.10)

project(changelogGenerator)

add_executable(changelogGenerator)

set(SOURCES
	changelogGenerator.cpp
)

target_sources(changelogGenerator PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(changelogGenerator PUBLIC baseLib)
	
set (outputDir "${CMAKE_SOURCE_DIR}/bin/tools")

set_property(TARGET changelogGenerator PROPERTY RUNTIME_OUTPUT_DIRECTORY "${outputDir}")

get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
		
if(isMultiConfig)
	set_property(TARGET changelogGenerator PROPERTY RUNTIME_OUTPUT_DIRECTORY_DEBUG "${outputDir}/Debug")
	set_property(TARGET changelogGenerator PROPERTY RUNTIME_OUTPUT_DIRECTORY_RELEASE "${outputDir}")
endif()

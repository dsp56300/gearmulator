cmake_minimum_required(VERSION 3.10)
project(mqLib)

add_library(mqLib STATIC)

set(SOURCES
	rom.cpp rom.h
	mqmc.cpp mqmc.h
)

target_sources(mqLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(mqLib PUBLIC synthLib 68kEmu)

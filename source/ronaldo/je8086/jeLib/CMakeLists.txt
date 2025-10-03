cmake_minimum_required(VERSION 3.10)

project(jeLib)

add_library(jeLib STATIC)

set(SOURCES
	device.cpp device.h
	je8086.cpp je8086.h
	je8086devices.cpp je8086devices.h
	jeLcd.cpp jeLcd.h
	jemiditypes.h
	jetypes.h
	patch.cpp patch.h
	rom.cpp rom.h
	romloader.cpp romloader.h
	state.cpp state.h
	sysexRemoteControl.cpp sysexRemoteControl.h
	types.h
)

target_sources(jeLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

set_property(TARGET jeLib PROPERTY FOLDER "JE8086")

target_link_libraries(jeLib PUBLIC synthLib hardwareLib rLib esp)

target_include_directories(jeLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

#target_compile_options(jeLib PRIVATE -fsanitize=thread)
#target_link_options(jeLib PUBLIC -fsanitize=thread)

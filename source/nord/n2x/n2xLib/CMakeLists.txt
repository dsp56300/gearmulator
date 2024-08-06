cmake_minimum_required(VERSION 3.10)

project(n2xLib)

add_library(n2xLib STATIC)

set(SOURCES
	n2xdevice.cpp n2xdevice.h
	n2xdsp.cpp n2xdsp.h
	n2xflash.cpp n2xflash.h
	n2xfrontpanel.cpp n2xfrontpanel.h
	n2xhardware.cpp n2xhardware.h
	n2xhdi08.cpp n2xhdi08.h
	n2xmc.cpp n2xmc.h
	n2xmiditypes.h
	n2xrom.cpp n2xrom.h
	n2xromdata.cpp n2xromdata.h
	n2xromloader.cpp n2xromloader.h
	n2xstate.cpp n2xstate.h
	n2xtypes.h
)

target_sources(n2xLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(n2xLib PUBLIC synthLib 68kEmu hardwareLib)

if(DSP56300_DEBUGGER)
	target_link_libraries(n2xLib PUBLIC dsp56kDebugger)
endif()

set_property(TARGET n2xLib PROPERTY FOLDER "N2x")

target_include_directories(n2xLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

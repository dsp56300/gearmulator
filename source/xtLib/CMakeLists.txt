cmake_minimum_required(VERSION 3.10)
project(xtLib)

add_library(xtLib STATIC)

set(SOURCES
	xt.cpp xt.h
	xtButtons.cpp xtButtons.h
	xtDevice.cpp xtDevice.h
	xtDSP.cpp xtDSP.h
	xtHardware.cpp xtHardware.h
	xtId.cpp xtId.h
	xtLcd.cpp xtLcd.h
	xtLeds.cpp xtLeds.h
	xtMidiTypes.h
	xtPic.cpp xtPic.h
	xtRom.cpp xtRom.h
	xtRomLoader.cpp xtRomLoader.h
	xtState.cpp xtState.h
	xtSysexRemoteControl.cpp xtSysexRemoteControl.h
	xtTypes.h
	xtUc.cpp xtUc.h
	xtWavePreview.cpp xtWavePreview.h
)

target_sources(xtLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(xtLib PUBLIC wLib)

if(DSP56300_DEBUGGER)
	target_link_libraries(xtLib PUBLIC dsp56kDebugger)
endif()

set_property(TARGET xtLib PROPERTY FOLDER "Xenia")

target_include_directories(xtLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

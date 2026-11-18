cmake_minimum_required(VERSION 3.15)

project(88emuClapHeadless)

# 88emu as a headless CLAP instrument, without JUCE: 88emuClapHeadless.clap, one plugin per device.
# Not for music production: no editor, parameters or saved state. It exists so that DOSBox Staging
# and other emulators can drive 88emu through their CLAP MIDI device. See clapEntry.cpp for what
# it needs from a host (nothing) and where it finds ROMs.
add_library(88emuClapHeadless MODULE clapEntry.cpp)

target_link_libraries(88emuClapHeadless PRIVATE 88lib)
target_include_directories(88emuClapHeadless PRIVATE ${CMAKE_SOURCE_DIR}/source/3rdparty/clap-juce-extensions/clap-libs/clap/include)

set_target_properties(88emuClapHeadless PROPERTIES
	PREFIX ""
	CXX_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN ON
	FOLDER "Ronaldo/88emu"
)

# Only clap_entry leaves the module: the cores, asmjit and the C++ runtime bits linked in
# statically must not bind to, or be bound by, symbols of the host.
if(APPLE)
	set_target_properties(88emuClapHeadless PROPERTIES
		BUNDLE TRUE
		BUNDLE_EXTENSION clap
		MACOSX_BUNDLE_GUI_IDENTIFIER com.theusualsuspects.88emuclapheadless
		MACOSX_BUNDLE_BUNDLE_NAME 88emuClapHeadless
		MACOSX_BUNDLE_BUNDLE_VERSION ${CMAKE_PROJECT_VERSION}
		MACOSX_BUNDLE_SHORT_VERSION_STRING ${CMAKE_PROJECT_VERSION}
	)
	target_link_options(88emuClapHeadless PRIVATE -Wl,-exported_symbol,_clap_entry)
elseif(WIN32)
	set_target_properties(88emuClapHeadless PROPERTIES SUFFIX ".clap")
else()
	set_target_properties(88emuClapHeadless PROPERTIES SUFFIX ".clap")
	file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/88emuClapHeadless.map "{ global: clap_entry; local: *; };\n")
	target_link_options(88emuClapHeadless PRIVATE -Wl,--version-script=${CMAKE_CURRENT_BINARY_DIR}/88emuClapHeadless.map)
endif()

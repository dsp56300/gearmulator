cmake_minimum_required(VERSION 3.15)

add_subdirectory(changelogLib)
add_subdirectory(tools/changelogGenerator)

# ----------------- Common libraries used by all synths

add_subdirectory(baseLib)
add_subdirectory(synthLib)
if(EMSCRIPTEN)
	# WebAssembly back ends for the JITs: only a JavaScript host can compile at run time.
	add_subdirectory(wasmJit)
endif()
add_subdirectory(tools/synthLibTests)
add_subdirectory(hardwareLib EXCLUDE_FROM_ALL)

# ----------------- network bridge

add_subdirectory(networkLib EXCLUDE_FROM_ALL)
add_subdirectory(tools/bridge)

# ----------------- Juce based plugin libraries, JUCE itself is added by source/CMakeLists.txt

if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN)
	add_subdirectory(juce/jucePluginLib EXCLUDE_FROM_ALL)
	# The MCP server is always built, it is enabled at runtime in the plugin settings
	add_subdirectory(juce/mcpServerLib EXCLUDE_FROM_ALL)
	add_subdirectory(juce/juceRmlUi)
	add_subdirectory(juce/juceRmlPlugin)
	add_subdirectory(juce/juceUiLib EXCLUDE_FROM_ALL)
	add_subdirectory(juce/jucePluginEditorLib EXCLUDE_FROM_ALL)
	add_subdirectory(juce/jucePluginData EXCLUDE_FROM_ALL)
	add_subdirectory(tools/pluginTester)
	add_subdirectory(tools/midiLearnTest)
	add_subdirectory(tools/jucePluginLibTests)

	# Only 88emu uses the player library, so it is built along with it, tests included. Excluded from
	# ALL, its tests were registered with ctest but never built.
	if(${CMAKE_PROJECT_NAME}_SYNTH_88EMU)
		add_subdirectory(juce/jucePlayerLib)
	endif()
endif()


cmake_minimum_required(VERSION 3.15)
project(jucePlugin VERSION ${CMAKE_PROJECT_VERSION}) 

option(${CMAKE_PROJECT_NAME}_BUILD_FX_PLUGIN "Build FX plugin variants" off)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.h.in ${CMAKE_CURRENT_SOURCE_DIR}/version.h)

if(JUCE_GLOBAL_VST2_SDK_PATH)
    set(VST "VST")
else()
    set(VST "")
endif()

set(SOURCES
	parameterDescriptions_C.json
	parameterDescriptions_Snow.json
	parameterDescriptions_TI.json
	ParameterNames.h
    PluginEditor.cpp
    PluginEditor.h
    PluginProcessor.cpp
    PluginProcessor.h
	VirusController.cpp
	VirusController.h
	VirusParameter.cpp
	VirusParameter.h
	VirusParameterBinding.cpp
	VirusParameterBinding.h
	version.h
)

set(SOURCES_UI3
	ui3/FxPage.cpp
	ui3/FxPage.h
	ui3/MidiPorts.cpp
	ui3/MidiPorts.h
    ui3/Parts.cpp
    ui3/Parts.h
	ui3/PatchBrowser.cpp
	ui3/PatchBrowser.h
    ui3/Tabs.cpp
    ui3/Tabs.h
    ui3/VirusEditor.cpp
    ui3/VirusEditor.h
)

# https://forum.juce.com/t/help-needed-using-binarydata-with-cmake-juce-6/40486
# "This might be because the BinaryData files are generated during the build, so the IDE may not be able to find them until the build has been run once (and even then, some IDEs might need a bit of a nudge to re-index the binary directory…)"
SET(ASSETS
	"parameterDescriptions_C.json"
	"parameterDescriptions_Snow.json"
	"parameterDescriptions_TI.json"
)

#include(skins/Galaxpel/assets.cmake)
#include(skins/Hoverland/assets.cmake)
#include(skins/Trancy/assets.cmake)
include(skins/TrancyTI/assets.cmake)

macro(createJucePlugin targetName productName isSynth plugin4CC binaryDataProject componentName)
	juce_add_plugin(${targetName}
		# VERSION ...                               # Set this if the plugin version is different to the project version
		# ICON_BIG ...                              # ICON_* arguments specify a path to an image file to use as an icon for the Standalone
		# ICON_SMALL ...
		COMPANY_NAME "The Usual Suspects"           # Specify the name of the plugin's author
		IS_SYNTH ${isSynth}                         # Is this a synth or an effect?
		NEEDS_MIDI_INPUT TRUE                       # Does the plugin need midi input?
		NEEDS_MIDI_OUTPUT TRUE                      # Does the plugin need midi output?
		IS_MIDI_EFFECT FALSE                        # Is this plugin a MIDI effect?
		EDITOR_WANTS_KEYBOARD_FOCUS TRUE            # Does the editor need keyboard focus?
		COPY_PLUGIN_AFTER_BUILD FALSE               # Should the plugin be installed to a default location after building?
		PLUGIN_MANUFACTURER_CODE TusP               # A four-character manufacturer id with at least one upper-case character
		PLUGIN_CODE ${plugin4CC}                    # A unique four-character plugin id with exactly one upper-case character
													# GarageBand 10.3 requires the first letter to be upper-case, and the remaining letters to be lower-case
		FORMATS AU VST3 ${VST} Standalone           # The formats to build. Other valid formats are: AAX Unity VST AU AUv3
		PRODUCT_NAME ${productName}                 # The name of the final executable, which can differ from the target name
	)

	target_sources(${targetName} PRIVATE ${SOURCES} ${SOURCES_UI3})

	source_group("source" FILES ${SOURCES})
	source_group("source\\ui3" FILES ${SOURCES_UI3})

	target_compile_definitions(${targetName} 
	PUBLIC
		# JUCE_WEB_BROWSER and JUCE_USE_CURL would be on by default, but you might not need them.
		JUCE_WEB_BROWSER=0  # If you remove this, add `NEEDS_WEB_BROWSER TRUE` to the `juce_add_plugin` call
		JUCE_USE_CURL=0     # If you remove this, add `NEEDS_CURL TRUE` to the `juce_add_plugin` call
		JUCE_VST3_CAN_REPLACE_VST2=0
		JUCE_WIN_PER_MONITOR_DPI_AWARE=0
	)

	target_link_libraries(${targetName}
	PRIVATE
		${binaryDataProject}
		juceUiLib
		jucePluginLib
		juce::juce_audio_utils
		juce::juce_cryptography
	PUBLIC
		virusLib
		#juce::juce_recommended_config_flags
		#juce::juce_recommended_lto_flags
		#juce::juce_recommended_warning_flags
	)

	if(UNIX AND NOT APPLE)
		target_link_libraries(${targetName} PUBLIC -static-libgcc -static-libstdc++)
	endif()

	if(MSVC OR APPLE)
		if(JUCE_GLOBAL_VST2_SDK_PATH)
			install(TARGETS ${targetName}_VST DESTINATION . COMPONENT VST2${componentName})
		endif()
		install(TARGETS ${targetName}_VST3 DESTINATION . COMPONENT VST3${componentName})
		if(APPLE)
			install(TARGETS ${targetName}_AU DESTINATION . COMPONENT AU${componentName})
		endif()
	elseif(UNIX)
		if(JUCE_GLOBAL_VST2_SDK_PATH)
			install(TARGETS ${targetName}_VST LIBRARY DESTINATION /usr/local/lib/lxvst/ COMPONENT VST2${componentName})
		endif()
		install(TARGETS ${targetName}_VST3 LIBRARY DESTINATION /usr/local/lib/vst3/ COMPONENT VST3${componentName})
	endif()
endmacro()

juce_add_binary_data(jucePlugin_BinaryData${postfix} SOURCES ${ASSETS} ${ASSETS_VirusC_Galaxpel} ${ASSETS_VirusC_Hoverland} ${ASSETS_VirusC_Trancy} ${ASSETS_VirusTI_Trancy})

createJucePlugin(jucePlugin "DSP56300Emu" TRUE "TusV" jucePlugin_BinaryData${postfix} "")
	
if(${CMAKE_PROJECT_NAME}_BUILD_FX_PLUGIN)
	createJucePlugin(jucePlugin_FX "DSP56300EmuFX" FALSE "TusF" jucePlugin_BinaryData${postfix} "_FX")
endif()

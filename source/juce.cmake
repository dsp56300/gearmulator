option(${CMAKE_PROJECT_NAME}_BUILD_FX_PLUGIN "Build FX plugin variants" off)

set(USE_CLAP ${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN_CLAP)

if(JUCE_GLOBAL_VST2_SDK_PATH)
    set(VST "VST")
else()
    set(VST "")
endif()  

macro(createJucePlugin targetName productName isSynth plugin4CC binaryDataProject synthLibProject componentName)
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

	target_sources(${targetName} PRIVATE ${SOURCES})

	source_group("source" FILES ${SOURCES})

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
		jucePluginEditorLib
		juce::juce_audio_utils
		juce::juce_cryptography
	PUBLIC
		${synthLibProject}
		#juce::juce_recommended_config_flags
		#juce::juce_recommended_lto_flags
		#juce::juce_recommended_warning_flags
	)

	set(clapFeatures "")
	if(${isSynth})
		list(APPEND clapFeatures instrument synthesizer)
	else()
		list(APPEND clapFeatures audio-effect synthesizer multi-effects)
	endif()

	if(USE_CLAP)
		clap_juce_extensions_plugin(TARGET ${targetName}
			CLAP_ID "com.theusualsuspects.${plugin4CC}"
			CLAP_FEATURES ${clapFeatures}
			CLAP_SUPPORT_URL "https://dsp56300.wordpress.com"
			CLAP_MANUAL_URL "https://dsp56300.wordpress.com"
			)
	endif()

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
		if(USE_CLAP)
			install(TARGETS ${targetName}_CLAP DESTINATION . COMPONENT CLAP${componentName})
		endif()
	elseif(UNIX)
		if(JUCE_GLOBAL_VST2_SDK_PATH)
			install(TARGETS ${targetName}_VST LIBRARY DESTINATION /usr/local/lib/vst/ COMPONENT VST2${componentName})
		endif()
		install(TARGETS ${targetName}_VST3 LIBRARY DESTINATION /usr/local/lib/vst3/ COMPONENT VST3${componentName})
		if(USE_CLAP)
			install(TARGETS ${targetName}_CLAP LIBRARY DESTINATION /usr/local/lib/clap/ COMPONENT CLAP${componentName})
		endif()
	endif()
endmacro()

macro(createJucePluginWithFX targetName productName plugin4CCSynth plugin4CCFX binaryDataProject synthLibProject)
	createJucePlugin(${targetName} "${productName}" TRUE "${plugin4CCSynth}" ${binaryDataProject} ${synthLibProject} "")
		
	if(${CMAKE_PROJECT_NAME}_BUILD_FX_PLUGIN)
		createJucePlugin(${targetName}_FX "${productName}FX" FALSE "${plugin4CCFX}" ${binaryDataProject} ${synthLibProject} "_FX")
	endif()
endmacro()

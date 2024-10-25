option(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN "Build Juce plugins" on)
option(${CMAKE_PROJECT_NAME}_BUILD_FX_PLUGIN "Build FX plugin variants" off)

option(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN_VST2 "Build VST2 version of Juce plugins" on)
option(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN_VST3 "Build VST3 version of Juce plugins" on)
option(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN_CLAP "Build CLAP version of Juce plugins" on)
option(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN_LV2 "Build LV2 version of Juce plugins" off)
option(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN_AU "Build AU version of Juce plugins" on)

set(USE_CLAP ${${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN_CLAP})
set(USE_LV2 ${${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN_LV2})
set(USE_VST2 ${${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN_VST2})
set(USE_VST3 ${${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN_VST3})
set(USE_AU ${${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN_AU})

set(JUCE_CMAKE_DIR ${CMAKE_CURRENT_LIST_DIR})

set(juce_formats "")

if(USE_AU)
	set(juce_formats AU)
endif()

if(USE_VST2 AND JUCE_GLOBAL_VST2_SDK_PATH)
    list(APPEND juce_formats VST)
endif()

if(USE_VST3)
    list(APPEND juce_formats VST3)
endif()

if(USE_LV2)
    list(APPEND juce_formats LV2)
endif()

macro(createJucePlugin targetName productName isSynth plugin4CC binaryDataProject synthLibProject)
	juce_add_plugin(${targetName}
		# VERSION ...                                     # Set this if the plugin version is different to the project version
		# ICON_BIG ...                                    # ICON_* arguments specify a path to an image file to use as an icon for the Standalone
		# ICON_SMALL ...
		COMPANY_NAME "The Usual Suspects"                 # Specify the name of the plugin's author
		IS_SYNTH ${isSynth}                               # Is this a synth or an effect?
		NEEDS_MIDI_INPUT TRUE                             # Does the plugin need midi input?
		NEEDS_MIDI_OUTPUT TRUE                            # Does the plugin need midi output?
		IS_MIDI_EFFECT FALSE                              # Is this plugin a MIDI effect?
		EDITOR_WANTS_KEYBOARD_FOCUS TRUE                  # Does the editor need keyboard focus?
		COPY_PLUGIN_AFTER_BUILD FALSE                     # Should the plugin be installed to a default location after building?
		PLUGIN_MANUFACTURER_CODE TusP                     # A four-character manufacturer id with at least one upper-case character
		PLUGIN_CODE ${plugin4CC}                          # A unique four-character plugin id with exactly one upper-case character
		                                                  # GarageBand 10.3 requires the first letter to be upper-case, and the remaining letters to be lower-case
		FORMATS ${juce_formats}                           # The formats to build. Other valid formats are: AAX Unity VST AU AUv3 LV2
		PRODUCT_NAME ${productName}                       # The name of the final executable, which can differ from the target name
		VST3_AUTO_MANIFEST TRUE                           # While generating a moduleinfo.json is nice, Juce does not properly package using cpack on Win/Linux
		                                                  # and completely fails on Linux if we change the suffix to .vst3, so we skip that completely for now
		BUNDLE_ID "com.theusualsuspects.${productName}"
		LV2URI "http://theusualsuspects.lv2.${productName}"
	)

	target_sources(${targetName} PRIVATE ${SOURCES})

	source_group("source" FILES ${SOURCES})

	target_compile_definitions(${targetName} 
	PUBLIC
		# JUCE_WEB_BROWSER and JUCE_USE_CURL would be on by default, but you might not need them.
		JUCE_WEB_BROWSER=0  # If you remove this, add `NEEDS_WEB_BROWSER TRUE` to the `juce_add_plugin` call
		JUCE_USE_CURL=0     # If you remove this, add `NEEDS_CURL TRUE` to the `juce_add_plugin` call
		JUCE_VST3_CAN_REPLACE_VST2=0
		JUCE_WIN_PER_MONITOR_DPI_AWARE=1
		JUCE_MODAL_LOOPS_PERMITTED=1
		JUCE_USE_OGGVORBIS=0
		JUCE_USE_MP3AUDIOFORMAT=0
		JUCE_USE_FLAC=0
		JUCE_USE_WINDOWS_MEDIA_FORMAT=0
	)

	target_link_libraries(${targetName}
	PUBLIC
		${binaryDataProject}
		jucePluginEditorLib
		${synthLibProject}
		juce::juce_audio_utils
		juce::juce_cryptography
		#juce::juce_recommended_config_flags
		#juce::juce_recommended_lto_flags
		#juce::juce_recommended_warning_flags
	)

	if(${isSynth})
		createMacSetupScript(${productName})
	endif()

	set(clapFeatures "")
	if(${isSynth})
		list(APPEND clapFeatures instrument synthesizer)
	else()
		list(APPEND clapFeatures audio-effect synthesizer multi-effects)
	endif()

	if(TARGET ${targetName}_rc_lib)
		set_property(TARGET ${targetName}_rc_lib PROPERTY FOLDER ${targetName})
	endif()

	if(TARGET ${binaryDataProject} AND ${isSynth})
		set_property(TARGET ${binaryDataProject} PROPERTY FOLDER ${targetName})
	endif()

	if(USE_CLAP)
		clap_juce_extensions_plugin(TARGET ${targetName}
			CLAP_ID "com.theusualsuspects.${plugin4CC}"
			CLAP_FEATURES ${clapFeatures}
			CLAP_SUPPORT_URL "https://dsp56300.wordpress.com"
			CLAP_MANUAL_URL "https://dsp56300.wordpress.com"
			CLAP_USE_JUCE_PARAMETER_RANGES "DISCRETE"
			)
		set_property(TARGET ${targetName}_CLAP PROPERTY FOLDER ${targetName})
		add_dependencies(${targetName}_All ${targetName}_CLAP)
	endif()

	if(UNIX AND NOT APPLE)
		target_link_libraries(${targetName} PUBLIC -static-libgcc -static-libstdc++)
	endif()
	
	if(USE_VST3)
		if(APPLE)
			install(TARGETS ${targetName}_VST3 DESTINATION . COMPONENT ${productName}-VST3)
			installMacSetupScript(. ${productName}-VST3)
		else()
			get_target_property(vst3OutputFolder ${targetName}_VST3 ARCHIVE_OUTPUT_DIRECTORY)
			if(UNIX)
				set(dest lib/vst3)
				set(pattern "*.so")
			else()
				set(dest .)
				set(pattern "*.vst3")
			endif()
			install(DIRECTORY ${vst3OutputFolder}/${productName}.vst3 DESTINATION ${dest} COMPONENT ${productName}-VST3 FILES_MATCHING PATTERN ${pattern} PATTERN "*.json")
		endif()
	endif()

	if(MSVC OR APPLE)
		if(USE_VST2 AND JUCE_GLOBAL_VST2_SDK_PATH)
			install(TARGETS ${targetName}_VST DESTINATION . COMPONENT ${productName}-VST2)
			if(APPLE)
				installMacSetupScript(. ${productName}-VST2)
			endif()
		endif()
		if(USE_AU AND APPLE)
			install(TARGETS ${targetName}_AU DESTINATION . COMPONENT ${productName}-AU)
			installMacSetupScript(. ${productName}-AU)
		endif()
		if(USE_CLAP)
			install(TARGETS ${targetName}_CLAP DESTINATION . COMPONENT ${productName}-CLAP)
			if(APPLE)
				installMacSetupScript(. ${productName}-CLAP)
			endif()
		endif()
	elseif(UNIX)
		if(USE_VST2 AND JUCE_GLOBAL_VST2_SDK_PATH)
			install(TARGETS ${targetName}_VST LIBRARY DESTINATION lib/vst/ COMPONENT ${productName}-VST2)
		endif()
		if(USE_CLAP)
			install(TARGETS ${targetName}_CLAP LIBRARY DESTINATION lib/clap/ COMPONENT ${productName}-CLAP)
		endif()
	endif()

	target_compile_definitions(${targetName} PUBLIC JucePlugin_Lv2Uri="$<TARGET_PROPERTY:${targetName},JUCE_LV2URI>")
	
	if(USE_LV2)
		get_target_property(lv2OutputFolder ${targetName}_LV2 ARCHIVE_OUTPUT_DIRECTORY)
		if(MSVC)
			set(pattern "*.dll")
		else()
			set(pattern "*.so")
		endif()
		if(MSVC OR APPLE)
			set(dest .)
		else()
			set(dest lib/lv2/)
		endif()
		install(DIRECTORY ${lv2OutputFolder}/${productName}.lv2 DESTINATION ${dest} COMPONENT ${productName}-LV2 FILES_MATCHING PATTERN ${pattern} PATTERN "*.ttl")
		if(APPLE)
			installMacSetupScript(${dest} ${productName}-LV2)
		endif()
	endif()

	if(USE_AU AND APPLE AND ${isSynth})
		add_test(NAME ${targetName}_AU_Validate COMMAND ${CMAKE_COMMAND} 
			-DIDCOMPANY=TusP
			-DIDPLUGIN=${plugin4CC}
			-DBINDIR=${CMAKE_BINARY_DIR}
			-DCOMPONENT_NAME=${productName}
			-DCPACK_FILE=${CPACK_PACKAGE_NAME}-${CMAKE_PROJECT_VERSION}-${CPACK_SYSTEM_NAME}-${productName}-AU.zip
			-P ${JUCE_CMAKE_DIR}/runAuValidation.cmake)
		set_tests_properties(${targetName}_AU_Validate PROPERTIES LABELS "PluginTest")
	endif()
endmacro()

macro(createJucePluginWithFX targetName productName plugin4CCSynth plugin4CCFX binaryDataProject synthLibProject)
	createJucePlugin(${targetName} "${productName}" TRUE "${plugin4CCSynth}" ${binaryDataProject} ${synthLibProject})

	if(${CMAKE_PROJECT_NAME}_BUILD_FX_PLUGIN)
		createJucePlugin(${targetName}_FX "${productName}FX" FALSE "${plugin4CCFX}" ${binaryDataProject} ${synthLibProject})
	endif()
endmacro()

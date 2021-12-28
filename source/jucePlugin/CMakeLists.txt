cmake_minimum_required(VERSION 3.15)
project(jucePlugin VERSION ${CMAKE_PROJECT_VERSION}) 

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.h.in ${CMAKE_CURRENT_SOURCE_DIR}/version.h)

if(JUCE_GLOBAL_VST2_SDK_PATH)
    set(VST "VST")
else()
    set(VST "")
endif()

juce_add_plugin(jucePlugin
    # VERSION ...                               # Set this if the plugin version is different to the project version
    # ICON_BIG ...                              # ICON_* arguments specify a path to an image file to use as an icon for the Standalone
    # ICON_SMALL ...
    COMPANY_NAME "The Usual Suspects"           # Specify the name of the plugin's author
    IS_SYNTH TRUE                               # Is this a synth or an effect?
    NEEDS_MIDI_INPUT TRUE                       # Does the plugin need midi input?
    NEEDS_MIDI_OUTPUT TRUE                      # Does the plugin need midi output?
    IS_MIDI_EFFECT FALSE                        # Is this plugin a MIDI effect?
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE            # Does the editor need keyboard focus?
    COPY_PLUGIN_AFTER_BUILD FALSE               # Should the plugin be installed to a default location after building?
    PLUGIN_MANUFACTURER_CODE TusP               # A four-character manufacturer id with at least one upper-case character
    PLUGIN_CODE TusV                            # A unique four-character plugin id with exactly one upper-case character
                                                # GarageBand 10.3 requires the first letter to be upper-case, and the remaining letters to be lower-case
    FORMATS AU VST3 ${VST} Standalone           # The formats to build. Other valid formats are: AAX Unity VST AU AUv3
    PRODUCT_NAME "DSP56300Emu"                  # The name of the final executable, which can differ from the target name
)

target_sources(jucePlugin 
PRIVATE
    PluginEditor.cpp
    PluginProcessor.cpp
    ui/Virus_Buttons.cpp
    ui/Virus_LookAndFeel.cpp
    ui/VirusEditor.cpp
    ui/Virus_ArpEditor.cpp
    ui/Virus_FxEditor.cpp
    ui/Virus_LfoEditor.cpp
    ui/Virus_OscEditor.cpp
    PluginEditor.h
    PluginProcessor.h
    ui/Virus_Buttons.h
    ui/Virus_LookAndFeel.h
    ui/VirusEditor.h
    ui/Virus_ArpEditor.h
    ui/Virus_FxEditor.h
    ui/Virus_LfoEditor.h
    ui/Virus_OscEditor.h
    ui/Ui_Utils.h
	version.h
)

# https://forum.juce.com/t/help-needed-using-binarydata-with-cmake-juce-6/40486
# "This might be because the BinaryData files are generated during the build, so the IDE may not be able to find them until the build has been run once (and even then, some IDEs might need a bit of a nudge to re-index the binary directory…)"
juce_add_binary_data(jucePlugin_BinaryData
    SOURCES
        "assets/bg_1377x800.png"
        "assets/panels/bg_arp_1018x620.png"
        "assets/panels/bg_fx_1018x620.png"
        "assets/panels/bg_lfo_1018x620.png"
        "assets/panels/bg_osc_1018x620.png"
        "assets/buttons/GLOBAL_btn_arp_settings_141x26.png"
        "assets/buttons/GLOBAL_btn_effects_141x26.png"
        "assets/buttons/GLOBAL_btn_lfo_matrix_141x26.png"
        "assets/buttons/GLOBAL_btn_osc_filter_141x26.png"
        "assets/buttons/env_pol_50x34.png"
        "assets/buttons/lfo_btn_23_19.png"
        "assets/buttons/link_vert_12x36.png"
        "assets/buttons/link_horizon_36x12.png"
        "assets/buttons/presets_btn_43_15.png"
        "assets/buttons/Handle_18x47.png"
        "assets/buttons/sync2_54x25.png"
        "assets/knobs/Gen_70x70_100.png"
        "assets/knobs/Gen_pol_70x70_100.png"
        "assets/knobs/GenBlue_70x70_100.png"
        "assets/knobs/GenRed_70x70_100.png"
        "assets/knobs/multi_18x18_100.png"
)

target_compile_definitions(jucePlugin 
PUBLIC
	# JUCE_WEB_BROWSER and JUCE_USE_CURL would be on by default, but you might not need them.
	JUCE_WEB_BROWSER=0  # If you remove this, add `NEEDS_WEB_BROWSER TRUE` to the `juce_add_plugin` call
	JUCE_USE_CURL=0     # If you remove this, add `NEEDS_CURL TRUE` to the `juce_add_plugin` call
	JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(jucePlugin 
PRIVATE
    jucePlugin_BinaryData
    juce::juce_audio_utils
PUBLIC
	virusLib
    #juce::juce_recommended_config_flags
    #juce::juce_recommended_lto_flags
    #juce::juce_recommended_warning_flags
)

if(UNIX AND NOT APPLE)
	target_link_libraries(jucePlugin PUBLIC -static-libgcc -static-libstdc++)
endif()

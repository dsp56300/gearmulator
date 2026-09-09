cmake_minimum_required(VERSION 3.15)

project(88emuplayer VERSION ${CMAKE_PROJECT_VERSION})

set(HARDWARE_SKIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)
set(HARDWARE_ASSETS
	emu88Player.rml
	emu88Player.rcss
	emu88PlayerSettings.rml
	emu88PlayerSettingsWindow.rml
	emu88PlayerSettings.rcss
	${HARDWARE_SKIN_DIR}/sc88_panel.png
	${HARDWARE_SKIN_DIR}/sc88vl_panel.png
	${HARDWARE_SKIN_DIR}/sc88pro_panel.png
	${HARDWARE_SKIN_DIR}/sc8850_panel.png
	${HARDWARE_SKIN_DIR}/sc55_panel.png
	${HARDWARE_SKIN_DIR}/sc8850_assets.png
	${HARDWARE_SKIN_DIR}/button_all.svg
	${HARDWARE_SKIN_DIR}/button_left.svg
	${HARDWARE_SKIN_DIR}/button_right.svg
	${HARDWARE_SKIN_DIR}/button_round.svg
	${HARDWARE_SKIN_DIR}/button_led_on.svg
	${HARDWARE_SKIN_DIR}/button_part_left.svg
	${HARDWARE_SKIN_DIR}/button_part_right.svg
	${HARDWARE_SKIN_DIR}/button_plain.svg
	${HARDWARE_SKIN_DIR}/preview.svg
	${HARDWARE_SKIN_DIR}/player_play.svg
	${HARDWARE_SKIN_DIR}/player_pause.svg
	${HARDWARE_SKIN_DIR}/player_stop.svg
	${HARDWARE_SKIN_DIR}/selector_led_off.svg
	${HARDWARE_SKIN_DIR}/selector_led_on.svg
	${HARDWARE_SKIN_DIR}/efx_led_off.svg
	${HARDWARE_SKIN_DIR}/efx_led_on.svg
	${HARDWARE_SKIN_DIR}/knob.png
	${HARDWARE_SKIN_DIR}/value_knob.png
)

juce_add_binary_data(88emuplayer_BinaryData SOURCES ${HARDWARE_ASSETS})

juce_add_plugin(88emuplayer
	# 512x512 is an exact .icns size and clears the 256 the .ico needs for its
	# largest entry - juceaide only emits a Windows size if a source image is at
	# least that big. Deliberately not in HARDWARE_ASSETS: it is the application
	# icon, not a skin asset, so it does not belong in the binary data blob.
	ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png"
	COMPANY_NAME "The Usual Suspects"
	COMPANY_WEBSITE "https://dsp56300.wordpress.com"
	IS_SYNTH TRUE
	NEEDS_MIDI_INPUT TRUE
	NEEDS_MIDI_OUTPUT TRUE
	IS_MIDI_EFFECT FALSE
	EDITOR_WANTS_KEYBOARD_FOCUS TRUE
	COPY_PLUGIN_AFTER_BUILD FALSE
	PLUGIN_MANUFACTURER_CODE TusP
	PLUGIN_CODE TscH
	FORMATS Standalone
	PRODUCT_NAME "88emuPlayer"
	PRODUCTS_FOLDER "${CMAKE_SOURCE_DIR}/bin/plugins/$<CONFIG>"
	BUNDLE_ID "com.theusualsuspects.SC88Hardware"
)

target_sources(88emuplayer PRIVATE
	Emu88Processor.cpp Emu88Processor.h
	Emu88Editor.cpp Emu88Editor.h
	Emu88EditorPlaylist.cpp
	Emu88EditorSettings.cpp
	Emu88EditorPanel.cpp
	Emu88EditorSkin.cpp
	Emu88EditorWindows.cpp
	Emu88EditorPlaylist.h
	Emu88EditorLcd.h
	Emu88EditorWindows.h
	Emu88EditorBindings.h
	Emu88AudioSettings.h
	Emu88MidiPlayer.cpp Emu88MidiPlayer.h
	Emu88PortMidiBridge.cpp Emu88PortMidiBridge.h
	panel.cpp panel.hpp
	midifile.hpp
	lcd_font.cpp
)

removeJuceDependencies(88emuplayer)
target_link_libraries(88emuplayer PRIVATE
	88emuplayer_BinaryData
	88lib
	juceRmlUi
	juceUiLib
	jucePluginData
	juce_plugin_modules
	portmidi
)
target_compile_definitions(88emuplayer PUBLIC JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
set_property(TARGET 88emuplayer PROPERTY FOLDER "Ronaldo/88emu")
set_property(TARGET 88emuplayer_BinaryData PROPERTY FOLDER "Ronaldo/88emu")

# Build the generated version header before compiling the player.
if(TARGET jucePluginLib)
	add_dependencies(88emuplayer jucePluginLib)
endif()

if(TARGET PluginFormat_Standalone)
	add_dependencies(PluginFormat_Standalone 88emuplayer_Standalone)
endif()

# The standalone player needs its own install rule and CPack component.
set(playerProductName "88emuPlayer")
createMacSetupScript(${playerProductName})

if(APPLE)
	install(TARGETS 88emuplayer_Standalone DESTINATION . COMPONENT ${playerProductName}-Standalone)
	# The .app is downloaded from a browser, so it arrives quarantined
	installMacSetupScript(. ${playerProductName}-Standalone)
elseif(MSVC)
	install(TARGETS 88emuplayer_Standalone RUNTIME DESTINATION . COMPONENT ${playerProductName}-Standalone)
else()
	install(TARGETS 88emuplayer_Standalone RUNTIME DESTINATION bin/ COMPONENT ${playerProductName}-Standalone)
endif()

set_target_properties(88emuplayer PROPERTIES TUS_PRODUCT_NAME "${playerProductName}")
tus_exportTarget(88emuplayer)

# createJucePlugin() ships the per-product changelog with every plugin component; do the
# same by hand here. The file is generated from doc/changelog.txt into a gitignored folder,
# so it is OPTIONAL - a source tree without a "${playerProductName}:" section still packages.
tus_registerChangelog(88emuplayer)
install(FILES "${CMAKE_SOURCE_DIR}/doc/changelog_split/changelog_${playerProductName}.txt"
	DESTINATION .
	COMPONENT ${playerProductName}-Standalone
	OPTIONAL)

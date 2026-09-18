cmake_minimum_required(VERSION 3.15)
project(88emuplayer VERSION ${CMAKE_PROJECT_VERSION})
set(HARDWARE_SKIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)
set(HARDWARE_ASSETS
	assets/emu88Player.rml
	assets/emu88Player.rcss
	assets/emu88PlayerSettings.rml
	assets/emu88PlayerSettingsWindow.rml
	assets/emu88PlayerSettings.rcss
	${HARDWARE_SKIN_DIR}/sc88_panel.png
	${HARDWARE_SKIN_DIR}/sc88vl_panel.png
	${HARDWARE_SKIN_DIR}/sc88pro_panel.png
	${HARDWARE_SKIN_DIR}/sc8850_panel.png
	${HARDWARE_SKIN_DIR}/sc55_panel.png
	${HARDWARE_SKIN_DIR}/sc55mk2_panel.png
	${HARDWARE_SKIN_DIR}/sc8820_panel.png
	${HARDWARE_SKIN_DIR}/cm32p_panel.png
	${HARDWARE_SKIN_DIR}/cm32l_panel.png
	${HARDWARE_SKIN_DIR}/cm64_panel.png
	${HARDWARE_SKIN_DIR}/sc88exp_panel.png
	${HARDWARE_SKIN_DIR}/sc55pc_panel.png
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
	${HARDWARE_SKIN_DIR}/player_play_dark.svg
	${HARDWARE_SKIN_DIR}/player_pause_dark.svg
	${HARDWARE_SKIN_DIR}/player_stop_dark.svg
	${HARDWARE_SKIN_DIR}/selector_led_off.svg
	${HARDWARE_SKIN_DIR}/selector_led_on.svg
	${HARDWARE_SKIN_DIR}/efx_led_off.svg
	${HARDWARE_SKIN_DIR}/efx_led_on.svg
	${HARDWARE_SKIN_DIR}/efx_led_green.svg
	${HARDWARE_SKIN_DIR}/efx_led_amber.svg
	${HARDWARE_SKIN_DIR}/cm_led_on.png
	${HARDWARE_SKIN_DIR}/knob.png
	${HARDWARE_SKIN_DIR}/value_knob.png
)
juce_add_binary_data(88emuplayer_BinaryData SOURCES ${HARDWARE_ASSETS})
juce_add_plugin(88emuplayer
	# The 512px application icon supplies both ICNS and Windows ICO sizes.
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
	app/Emu88LaunchOptions.cpp app/Emu88LaunchOptions.h
	ui/Emu88Editor.cpp ui/Emu88Editor.h
	ui/Emu88EditorPlaylist.cpp
	ui/Emu88EditorSettings.cpp
	ui/Emu88EditorAudioMidi.cpp
	ui/Emu88EditorPanel.cpp
	ui/Emu88EditorSkin.cpp
	ui/Emu88EditorWindows.cpp
	ui/Emu88EditorPlaylist.h
	ui/Emu88EditorLcd.h
	ui/Emu88EditorWindows.h
	ui/Emu88EditorBindings.h
	ui/panel.cpp ui/panel.hpp
	ui/lcd_font.cpp
)
removeJuceDependencies(88emuplayer)
target_link_libraries(88emuplayer PRIVATE
	88emuplayer_BinaryData
	88lib
	jucePlayerLib
	juceMidiPorts
	juceRmlUi
	juceUiLib
	jucePluginData
	juce_plugin_modules
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
# The standalone player needs its own install rule and CPack component. No macOS setup script: the
# README explains how to allow the quarantined app and CLI in System Settings.
set(playerProductName "88emuPlayer")
if(APPLE)
	install(TARGETS 88emuplayer_Standalone DESTINATION . COMPONENT ${playerProductName}-Standalone)
elseif(MSVC)
	install(TARGETS 88emuplayer_Standalone RUNTIME DESTINATION . COMPONENT ${playerProductName}-Standalone)
else()
	install(TARGETS 88emuplayer_Standalone RUNTIME DESTINATION bin/ COMPONENT ${playerProductName}-Standalone)
endif()
set_target_properties(88emuplayer PROPERTIES TUS_PRODUCT_NAME "${playerProductName}")
tus_exportTarget(88emuplayer)
# The generated changelog may be absent when this product has no entries.
tus_registerChangelog(88emuplayer)
install(FILES "${CMAKE_SOURCE_DIR}/LICENSE.md"
	DESTINATION .
	COMPONENT ${playerProductName}-Standalone)
install(FILES "${CMAKE_SOURCE_DIR}/doc/88emu_readme.md"
	DESTINATION .
	RENAME README.md
	COMPONENT ${playerProductName}-Standalone)
install(FILES "${CMAKE_SOURCE_DIR}/doc/changelog_split/changelog_${playerProductName}.txt"
	DESTINATION .
	COMPONENT ${playerProductName}-Standalone
	OPTIONAL)
# Keep JUCE's wrapper, but own startup configuration before it opens devices.
target_sources(88emuplayer_Standalone PRIVATE app/Emu88Standalone.cpp)
target_compile_definitions(88emuplayer PUBLIC JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
add_executable(88EmuCli app/Emu88Cli.cpp app/Emu88LaunchOptions.cpp)
target_link_libraries(88EmuCli PRIVATE 88lib jucePlayerLib juce_plugin_modules)
target_compile_definitions(88EmuCli PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
set_target_properties(88EmuCli PROPERTIES FOLDER "Ronaldo/88emu" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/plugins/$<CONFIG>")
if(WIN32)
	target_link_libraries(88EmuCli PRIVATE shell32)
endif()
if(TARGET PluginFormat_Standalone)
	add_dependencies(PluginFormat_Standalone 88EmuCli)
endif()
if(APPLE OR WIN32)
	install(TARGETS 88EmuCli RUNTIME DESTINATION . COMPONENT ${playerProductName}-Standalone)
else()
	install(TARGETS 88EmuCli RUNTIME DESTINATION bin COMPONENT ${playerProductName}-Standalone)
endif()
if(BUILD_TESTING)
	add_executable(88emuLaunchOptions_test tests/Emu88LaunchOptions_test.cpp app/Emu88LaunchOptions.cpp)
	target_link_libraries(88emuLaunchOptions_test PRIVATE 88lib jucePlayerLib)
	add_test(NAME 88emuLaunchOptions COMMAND 88emuLaunchOptions_test)
	set_property(TARGET 88emuLaunchOptions_test PROPERTY FOLDER "Ronaldo/tests")
endif()

# Numeric argument validation must retain NaN/Inf checks in Release.
foreach(target IN ITEMS 88emuplayer 88EmuCli 88emuLaunchOptions_test)
	if(TARGET ${target})
		if(MSVC)
			target_compile_options(${target} PRIVATE /fp:precise)
		else()
			target_compile_options(${target} PRIVATE -fno-finite-math-only)
		endif()
	endif()
endforeach()

cmake_minimum_required(VERSION 3.15)

project(tusInstaller VERSION ${CMAKE_PROJECT_VERSION})

set(SOURCES
	main.cpp

	bulkUninstall.cpp bulkUninstall.h
	changelogPage.cpp changelogPage.h
	detailPage.cpp detailPage.h
	installerApp.cpp installerApp.h
	installTask.cpp installTask.h
	installerDataProvider.cpp installerDataProvider.h
	installerDocument.cpp installerDocument.h
	loginDialog.cpp loginDialog.h
	mainWindow.cpp mainWindow.h
	pluginEntryController.cpp pluginEntryController.h
	preferencesPage.cpp preferencesPage.h

	resources/installer.rml
	resources/installer.rcss
)

juce_add_binary_data(installerBinaryData SOURCES
	resources/installer.rml
	resources/installer.rcss
	resources/Roboto-VariableFont_wdth_wght.ttf
	resources/placeholder.png

	# One white SVG per icon, tinted per state with image-color: a vector scales with the UI,
	# and one file covers every colour a button puts it in.
	resources/icons/icon_check.svg
	resources/icons/icon_downgrade.svg
	resources/icons/icon_eye.svg
	resources/icons/icon_folder.svg
	resources/icons/icon_install.svg
	resources/icons/icon_login.svg
	resources/icons/icon_refresh.svg
	resources/icons/icon_reinstall.svg
	resources/icons/icon_remove.svg
	resources/icons/icon_settings.svg
	resources/icons/icon_update.svg
	resources/icons/icon_upgrade.svg
)

juce_add_gui_app(tusInstaller
	COMPANY_NAME "The Usual Suspects"
	BUNDLE_ID "com.theusualsuspects.tusInstaller"
	PRODUCT_NAME "TUS Plugin Manager"
	NEEDS_CURL ${TUS_PM_NEEDS_CURL}

	# JUCE builds the .ico and the .icns from these, so they are PNGs with an alpha channel
	# rather than platform icon files.
	ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/appIcon.png"
	ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/resources/appIcon_small.png"
)

juce_generate_juce_header(tusInstaller)

target_include_directories(tusInstaller PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_sources(tusInstaller PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(tusInstaller PRIVATE
	installerBinaryData
	pluginManagerLib
	mcpServerLib
	juceRmlUi
	juceUiLib
	baseLib
	juce::juce_gui_extra
	juce::juce_opengl
PUBLIC
	juce::juce_recommended_config_flags
	juce::juce_recommended_warning_flags
)

target_compile_definitions(tusInstaller PRIVATE
	JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
	JUCE_WEB_BROWSER=0
	JUCE_USE_CURL=${TUS_PM_USE_CURL}
	JUCE_DISPLAY_SPLASH_SCREEN=0  # Permitted by the JUCE 7 EULA because we ship under the GPLv3
	JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:tusInstaller,JUCE_PRODUCT_NAME>"
	JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:tusInstaller,JUCE_VERSION>"
)

set_property(TARGET tusInstaller PROPERTY FOLDER "Gearmulator")
set_property(TARGET installerBinaryData PROPERTY FOLDER "Gearmulator")

# Packaged like any other product we ship: an install rule puts it in a CPack component, which
# cpack turns into TheUsualSuspects-PluginManager-<version>-<platform>.zip, and the export tells
# scripts/deployAll.cmake which folder to upload that to. A bundle on macOS, a bare program on
# Windows, and under bin/ on Linux so the deb and rpm put it on the PATH.
if(APPLE)
	install(TARGETS tusInstaller DESTINATION . COMPONENT ${TUS_PM_COMPONENT})
elseif(WIN32)
	install(TARGETS tusInstaller RUNTIME DESTINATION . COMPONENT ${TUS_PM_COMPONENT})
else()
	install(TARGETS tusInstaller RUNTIME DESTINATION bin/ COMPONENT ${TUS_PM_COMPONENT})
endif()

install(FILES "${CMAKE_SOURCE_DIR}/LICENSE.md" DESTINATION . COMPONENT ${TUS_PM_COMPONENT})

install(FILES "${CMAKE_SOURCE_DIR}/doc/pluginmanager_readme.md"
	DESTINATION .
	RENAME README.md
	COMPONENT ${TUS_PM_COMPONENT})

# No per-product changelog in the archive, although doc/changelog.txt has a section for this and
# the generator duly writes one. Every such file carries the Framework section as well, which is
# about the plugins - knobs, meters, audio - and says nothing about the program installing them.
# The README above is what belongs here; the release notes are where the rest is read.

set_target_properties(tusInstaller PROPERTIES TUS_PRODUCT_NAME "${TUS_PM_COMPONENT}")
tus_exportTarget(tusInstaller)

cmake_minimum_required(VERSION 3.15)

project(jucePluginData)

set(SOURCES
	arrow_drop_down.png
	arrow_up.png
	arrow_right.png
	arrow_down.png
	arrow_left.png
	check.png
	chevron_right.png
	circle.png
	circle_border.png
	link.png
	lock.png
	pm-dot.png

	Roboto-VariableFont_wdth_wght.ttf
	Roboto-Italic-VariableFont_wdth_wght.ttf

	tus_colorpicker.rcss
	tus_colorpicker.rml
	tus_default.rcss
	tus_juceskin.rcss
	tus_patchmanager.rcss
	tus_patchmanager.rml
)

juce_add_binary_data(jucePluginData HEADER_NAME jucePluginData.h NAMESPACE jucePluginData SOURCES ${SOURCES})
set_property(TARGET jucePluginData PROPERTY FOLDER "Gearmulator")

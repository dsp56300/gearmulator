cmake_minimum_required(VERSION 3.15)

project(jucePluginData)

set(SOURCES
	link.png
	lock.png
	check.png
	chevron_right.png
	arrow_drop_down.png
	arrow_left.png
	arrow_right.png
	Roboto-VariableFont_wdth_wght.ttf
	Roboto-Italic-VariableFont_wdth_wght.ttf
)

juce_add_binary_data(jucePluginData HEADER_NAME jucePluginData.h NAMESPACE jucePluginData SOURCES ${SOURCES})
set_property(TARGET jucePluginData PROPERTY FOLDER "Gearmulator")

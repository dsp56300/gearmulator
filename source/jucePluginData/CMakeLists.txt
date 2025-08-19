cmake_minimum_required(VERSION 3.15)

project(jucePluginData)

set(DATASOURCES
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
)

set(RMLSOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/tus_colorpicker.rcss
	${CMAKE_CURRENT_SOURCE_DIR}/tus_colorpicker.rml
	${CMAKE_CURRENT_SOURCE_DIR}/tus_default.rcss
	${CMAKE_CURRENT_SOURCE_DIR}/tus_juceskin.rcss
	${CMAKE_CURRENT_SOURCE_DIR}/tus_patchmanager.rcss
	${CMAKE_CURRENT_SOURCE_DIR}/tus_patchmanager.rml
)

list(APPEND DATASOURCES ${RMLSOURCES})

add_custom_target(rmlData SOURCES ${RMLSOURCES})
set_property(TARGET rmlData PROPERTY FOLDER "Gearmulator")

juce_add_binary_data(jucePluginData HEADER_NAME jucePluginData.h NAMESPACE jucePluginData SOURCES ${DATASOURCES})
set_property(TARGET jucePluginData PROPERTY FOLDER "Gearmulator")

add_dependencies(jucePluginData rmlData)
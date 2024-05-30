cmake_minimum_required(VERSION 3.15)

project(jucePluginData)

set(SOURCES
	link.png
	lock.png
)

juce_add_binary_data(jucePluginData HEADER_NAME jucePluginData.h NAMESPACE jucePluginData SOURCES ${SOURCES})
set_property(TARGET jucePluginData PROPERTY FOLDER "Gearmulator")

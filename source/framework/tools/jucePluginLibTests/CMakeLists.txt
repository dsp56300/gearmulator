cmake_minimum_required(VERSION 3.10)

project(jucePluginLibTests VERSION ${CMAKE_PROJECT_VERSION})

set(SOURCES
	jucePluginLibTests.cpp
	midiNotifierTest.cpp
	skinVariablesTest.cpp
)

juce_add_console_app(jucePluginLibTests
	COMPANY_NAME "The Usual Suspects"
	COMPANY_WEBSITE "https://dsp56300.com"
	PRODUCT_NAME "jucePluginLibTests"
	BUNDLE_ID "com.theusualsuspects.jucepluginlibtests"
)

juce_generate_juce_header(jucePluginLibTests)

target_compile_definitions(jucePluginLibTests PRIVATE
	JUCE_USE_CURL=0
	JUCE_WEB_BROWSER=0
)

target_sources(jucePluginLibTests PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(jucePluginLibTests PUBLIC jucePluginLib juce::juce_core juce::juce_graphics)

add_test(NAME jucePluginLibTests COMMAND jucePluginLibTests)
set_tests_properties(jucePluginLibTests PROPERTIES LABELS "UnitTest")

set_property(TARGET jucePluginLibTests PROPERTY FOLDER "Gearmulator")

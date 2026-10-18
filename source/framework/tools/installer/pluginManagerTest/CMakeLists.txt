cmake_minimum_required(VERSION 3.15)

project(pluginManagerTest VERSION ${CMAKE_PROJECT_VERSION})

set(SOURCES pluginManagerTest.cpp)

juce_add_console_app(pluginManagerTest
	COMPANY_NAME "The Usual Suspects"
	COMPANY_WEBSITE "https://dsp56300.com"
	PRODUCT_NAME "pluginManagerTest"
	BUNDLE_ID "com.theusualsuspects.pluginmanagertest"
)

juce_generate_juce_header(pluginManagerTest)

target_compile_definitions(pluginManagerTest PRIVATE
	JUCE_USE_CURL=0
	JUCE_WEB_BROWSER=0
)

target_sources(pluginManagerTest PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(pluginManagerTest PUBLIC pluginManagerLib juce::juce_core juce::juce_events)

add_test(NAME pluginManagerTests COMMAND pluginManagerTest)
set_tests_properties(pluginManagerTests PROPERTIES LABELS "UnitTest")

set_property(TARGET pluginManagerTest PROPERTY FOLDER "Gearmulator")

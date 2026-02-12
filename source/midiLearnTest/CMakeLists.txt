cmake_minimum_required(VERSION 3.10)

project(midiLearnTest VERSION ${CMAKE_PROJECT_VERSION})

set(SOURCES midiLearnTest.cpp)

juce_add_console_app(midiLearnTest
	COMPANY_NAME "The Usual Suspects"
	COMPANY_WEBSITE "https://dsp56300.com"
	PRODUCT_NAME "midiLearnTest"
)

juce_generate_juce_header(midiLearnTest)

target_sources(midiLearnTest PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(midiLearnTest PUBLIC jucePluginLib juce::juce_core juce::juce_graphics)

add_test(NAME midiLearnTests COMMAND midiLearnTest)
set_tests_properties(midiLearnTests PROPERTIES LABELS "UnitTest")

set_property(TARGET midiLearnTest PROPERTY FOLDER "Gearmulator")

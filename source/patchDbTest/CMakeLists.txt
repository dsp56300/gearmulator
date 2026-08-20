cmake_minimum_required(VERSION 3.10)

project(patchDbTest VERSION ${CMAKE_PROJECT_VERSION})

set(SOURCES patchDbTest.cpp)

juce_add_console_app(patchDbTest
	COMPANY_NAME "The Usual Suspects"
	COMPANY_WEBSITE "https://dsp56300.com"
	PRODUCT_NAME "patchDbTest"
	BUNDLE_ID "com.theusualsuspects.patchdbtest"
)

juce_generate_juce_header(patchDbTest)

target_compile_definitions(patchDbTest PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
)

target_sources(patchDbTest PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(patchDbTest PUBLIC jucePluginLib juce::juce_core juce::juce_graphics)

add_test(NAME patchDbTests COMMAND patchDbTest)
set_tests_properties(patchDbTests PROPERTIES LABELS "UnitTest")

set_property(TARGET patchDbTest PROPERTY FOLDER "Gearmulator")

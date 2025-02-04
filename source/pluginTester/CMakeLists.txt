cmake_minimum_required(VERSION 3.15)

project(pluginTester VERSION ${CMAKE_PROJECT_VERSION}) 

set(SOURCES
	fakeAudioDevice.cpp fakeAudioDevice.h
	logger.cpp logger.h
	pluginTester.cpp
	pluginHost.cpp
	pluginHost.h
)

juce_add_console_app(pluginTester
	COMPANY_NAME "The Usual Suspects"                 # Specify the name of the plugin's author
	COMPANY_WEBSITE "https://dsp56300.com"
	FORMATS VST VST3 AU LV2 CLAP                      # The formats to build. Other valid formats are: AAX Unity VST AU AUv3 LV2
	PRODUCT_NAME "pluginTester"                       # The name of the final executable, which can differ from the target name
)

juce_generate_juce_header(pluginTester)

target_link_libraries(pluginTester PRIVATE juce::juce_audio_utils)

target_sources(pluginTester PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(pluginTester PUBLIC baseLib)

target_compile_definitions(pluginTester PRIVATE 
	JUCE_PLUGINHOST_VST=1
	JUCE_PLUGINHOST_VST3=1
	JUCE_PLUGINHOST_CLAP=1
	JUCE_PLUGINHOST_LV2=1
	JUCE_PLUGINHOST_AU=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
)

set_property(TARGET pluginTester PROPERTY FOLDER "Gearmulator")

cmake_minimum_required(VERSION 3.15)

project(tuspm VERSION ${CMAKE_PROJECT_VERSION})

set(SOURCES tuspm.cpp)

juce_add_console_app(tuspm
	COMPANY_NAME "The Usual Suspects"
	COMPANY_WEBSITE "https://dsp56300.com"
	PRODUCT_NAME "tuspm"
	BUNDLE_ID "com.theusualsuspects.tuspm"
	NEEDS_CURL ${TUS_PM_NEEDS_CURL}
)

juce_generate_juce_header(tuspm)

target_compile_definitions(tuspm PRIVATE
	JUCE_USE_CURL=${TUS_PM_USE_CURL}
	JUCE_WEB_BROWSER=0
	JUCE_DISPLAY_SPLASH_SCREEN=0  # Permitted by the JUCE 7 EULA because we ship under the GPLv3
)

target_sources(tuspm PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(tuspm PUBLIC pluginManagerLib juce::juce_core juce::juce_events)

set_property(TARGET tuspm PROPERTY FOLDER "Gearmulator")

# Into the same archive as the GUI: it is the same program driven from a script, and a machine
# that wants one usually wants the other.
if(WIN32 OR APPLE)
	install(TARGETS tuspm RUNTIME DESTINATION . COMPONENT ${TUS_PM_COMPONENT})
else()
	install(TARGETS tuspm RUNTIME DESTINATION bin/ COMPONENT ${TUS_PM_COMPONENT})
endif()

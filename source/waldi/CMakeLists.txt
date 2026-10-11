cmake_minimum_required(VERSION 3.15)

# Vavra is still added by source/CMakeLists.txt, together with the 3rdparty folders its test console needs

if(${CMAKE_PROJECT_NAME}_SYNTH_XENIA)
	add_subdirectory(xt/xtLib EXCLUDE_FROM_ALL)
	add_subdirectory(xt/xtTestConsole)

	if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN)
		add_subdirectory(xt/xtJucePlugin)
	endif()
endif()

if(${CMAKE_PROJECT_NAME}_SYNTH_VAVRA OR ${CMAKE_PROJECT_NAME}_SYNTH_XENIA)
	add_subdirectory(common/wFlashTest)
endif()

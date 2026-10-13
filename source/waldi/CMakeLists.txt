cmake_minimum_required(VERSION 3.15)

add_subdirectory(common/wLib EXCLUDE_FROM_ALL)

# ----------------- Vavra

if(${CMAKE_PROJECT_NAME}_SYNTH_VAVRA)
	add_subdirectory(common/mqLib EXCLUDE_FROM_ALL)
	add_subdirectory(common/mqConsoleLib EXCLUDE_FROM_ALL)
	add_subdirectory(microq/mqTestConsole)
	add_subdirectory(microq/mqPerformanceTest)

	if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN)
		add_subdirectory(microq/mqJucePlugin)
	endif()
endif()

# ----------------- Xenia

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

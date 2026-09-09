cmake_minimum_required(VERSION 3.15)

add_subdirectory(common)

if(${CMAKE_PROJECT_NAME}_SYNTH_JE8086)
	add_subdirectory(esp)
	add_subdirectory(je8086)
endif()

if(${CMAKE_PROJECT_NAME}_SYNTH_88EMU)
	add_subdirectory(custom_chips/xp)
	add_subdirectory(custom_chips/gp)
	add_subdirectory(88emu)
endif()

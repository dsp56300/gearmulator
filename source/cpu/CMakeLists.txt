cmake_minimum_required(VERSION 3.15)

# The DSP56300 emulator has its own test suite and nearly every product uses it, so it stays in ALL. Every other core
# is built only when a product links it

add_subdirectory(dsp56300/source)

add_subdirectory(h8s EXCLUDE_FROM_ALL)
add_subdirectory(mc68k EXCLUDE_FROM_ALL)

# mc68k does not declare an include root of its own, so #include "mc68k/..."
# used to resolve only through the blanket source/ root that baseLib exported.
# Hand it the cpu/ root here until the submodule declares it itself.
target_include_directories(68kEmu PUBLIC ${CMAKE_CURRENT_LIST_DIR})

# A product that uses the ColdFire sets COLDFIRE_TESTS before this, which registers its test
add_subdirectory(coldfire EXCLUDE_FROM_ALL)

# the cores of 88emu, their tests are built along with it
if(${CMAKE_PROJECT_NAME}_SYNTH_88EMU)
	add_subdirectory(common EXCLUDE_FROM_ALL)
	add_subdirectory(h8500 EXCLUDE_FROM_ALL)
	add_subdirectory(mcs96 EXCLUDE_FROM_ALL)
	add_subdirectory(sh2 EXCLUDE_FROM_ALL)
endif()

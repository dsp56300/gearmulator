cmake_minimum_required(VERSION 3.10)
project(hardwareLib)

add_library(hardwareLib STATIC)

set(SOURCES
	am29f.cpp am29f.h
	i2c.cpp i2c.h
	i2cFlash.cpp i2cFlash.h
	hd44780.cpp hd44780.h
	lcd.cpp lcd.h
	lcdfonts.cpp lcdfonts.h
	sciMidi.cpp sciMidi.h
	sed1335.cpp sed1335.h
	tc160g22af.h
)

target_sources(hardwareLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(hardwareLib PUBLIC 68kEmu synthLib)

set_property(TARGET hardwareLib PROPERTY FOLDER "Gearmulator")

target_include_directories(hardwareLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(BUILD_TESTING)
	add_executable(hd44780_test hd44780_test.cpp)
	target_link_libraries(hd44780_test PRIVATE hardwareLib)
	# This directory is pulled in with EXCLUDE_FROM_ALL, which would keep the test out of the
	# generated solution and leave ctest reporting it as "Not Run" forever.
	set_property(TARGET hd44780_test PROPERTY EXCLUDE_FROM_ALL FALSE)
	set_property(TARGET hd44780_test PROPERTY FOLDER "Tests")
	add_test(NAME hd44780 COMMAND hd44780_test)

	add_executable(sed1335_test sed1335_test.cpp)
	target_link_libraries(sed1335_test PRIVATE hardwareLib)
	set_property(TARGET sed1335_test PROPERTY EXCLUDE_FROM_ALL FALSE)
	set_property(TARGET sed1335_test PROPERTY FOLDER "Tests")
	add_test(NAME sed1335 COMMAND sed1335_test)
endif()

#target_compile_options(hardwareLib PRIVATE -fsanitize=thread)
#target_link_options(hardwareLib PUBLIC -fsanitize=thread)

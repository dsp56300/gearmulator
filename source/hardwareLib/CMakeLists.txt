cmake_minimum_required(VERSION 3.10)
project(hardwareLib)

add_library(hardwareLib STATIC)

set(SOURCES
	am29f.cpp am29f.h
	i2c.cpp i2c.h
	i2cFlash.cpp i2cFlash.h
	lcd.cpp lcd.h
	lcdfonts.cpp lcdfonts.h
	sciMidi.cpp sciMidi.h
)

target_sources(hardwareLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(hardwareLib PUBLIC 68kEmu synthLib)

set_property(TARGET hardwareLib PROPERTY FOLDER "Gearmulator")

target_include_directories(hardwareLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

#target_compile_options(hardwareLib PRIVATE -fsanitize=thread)
#target_link_options(hardwareLib PUBLIC -fsanitize=thread)

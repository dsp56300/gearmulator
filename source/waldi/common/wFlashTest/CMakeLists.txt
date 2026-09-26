cmake_minimum_required(VERSION 3.10)

project(wFlashTest)

# one executable per machine, both define the Musashi callbacks

if(TARGET mqLib)
	add_executable(mqFlashTest wFlashTest.cpp)
	target_compile_definitions(mqFlashTest PRIVATE FLASHTEST_MQ)
	target_link_libraries(mqFlashTest PRIVATE mqLib)
	set_property(TARGET mqFlashTest PROPERTY FOLDER "Vavra")
endif()

if(TARGET xtLib)
	add_executable(xtFlashTest wFlashTest.cpp)
	target_compile_definitions(xtFlashTest PRIVATE FLASHTEST_XT)
	target_link_libraries(xtFlashTest PRIVATE xtLib)
	set_property(TARGET xtFlashTest PROPERTY FOLDER "Xenia")
endif()

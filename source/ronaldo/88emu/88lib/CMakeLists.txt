cmake_minimum_required(VERSION 3.10)

project(88lib)

add_library(88lib STATIC)

set(SOURCES
	analog/analogOutput.cpp analog/analogOutput.h
	analog/analogModels.cpp analog/analogModels.h
	deviceModel.cpp deviceModel.h
	boards/cm32l.cpp boards/cm32l.h
	boards/cm32p.cpp boards/cm32p.h
	boards/cm64.cpp boards/cm64.h
	boards/miig5.cpp boards/miig5.h
	boards/nu10b.cpp boards/nu10b.h
	hardwareDevice.cpp hardwareDevice.h
	rom/rom.cpp rom/rom.h
	rom/romloader.cpp rom/romloader.h
	rom/romInventory.cpp rom/romScanner.cpp
	rom/romRegistry.h
	boards/sc55Board.cpp boards/sc55Board.h
	mcu/sc55_submcu.cpp mcu/sc55_submcu.h
	boards/sc88.cpp boards/sc88.h
	boards/sc8820.cpp boards/sc8820.h
	boards/sc8850.cpp boards/sc8850.h
	mcu/sc8850_submcu.cpp mcu/sc8850_submcu.h
	boards/sc88types.h
	mcu/sc88_submcu.cpp mcu/sc88_submcu.h
	boards/sc88pro.cpp boards/sc88pro.h
)

target_sources(88lib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

set_property(TARGET 88lib PROPERTY FOLDER "Ronaldo/88emu")

target_link_libraries(88lib PUBLIC synthLib hardwareLib h8500 sh2 mcs96 xpLib gpLib lpLib rccLib mt32ReverbLib asmjit)

target_include_directories(88lib
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

if(BUILD_TESTING)
	add_executable(hardwareDevice_test tests/hardwareDevice_test.cpp)
	target_link_libraries(hardwareDevice_test PRIVATE 88lib)
	if(TARGET jucePlayerLib)
		target_link_libraries(hardwareDevice_test PRIVATE jucePlayerLib)
		target_compile_definitions(hardwareDevice_test PRIVATE TEST_JUCE_INPUT=1)
	endif()
	set_property(TARGET hardwareDevice_test PROPERTY FOLDER "Ronaldo/tests")
	add_test(NAME hardwareDevice COMMAND hardwareDevice_test)
	set_tests_properties(hardwareDevice PROPERTIES TIMEOUT 20)

	add_executable(analogOutput_test tests/analogOutput_test.cpp)
	target_link_libraries(analogOutput_test PRIVATE 88lib)
	set_property(TARGET analogOutput_test PROPERTY FOLDER "Ronaldo/tests")
	add_test(NAME analogOutput COMMAND analogOutput_test)
	set_tests_properties(analogOutput PROPERTIES TIMEOUT 20)

	add_executable(rom_test tests/rom_test.cpp)
	target_link_libraries(rom_test PRIVATE 88lib)
	set_property(TARGET rom_test PROPERTY FOLDER "Ronaldo/tests")
	add_test(NAME rom COMMAND rom_test)
	set_tests_properties(rom PROPERTIES TIMEOUT 20)

	add_executable(cm32p_test tests/cm32p_test.cpp)
	target_link_libraries(cm32p_test PRIVATE 88lib)
	set_property(TARGET cm32p_test PROPERTY FOLDER "Ronaldo/tests")
	add_test(NAME cm32p COMMAND cm32p_test)
	set_tests_properties(cm32p PROPERTIES TIMEOUT 20)

	add_executable(sc8850_submcu_test tests/sc8850_submcu_test.cpp)
	target_link_libraries(sc8850_submcu_test PRIVATE 88lib)
	set_property(TARGET sc8850_submcu_test PROPERTY EXCLUDE_FROM_ALL FALSE)
	set_property(TARGET sc8850_submcu_test PROPERTY FOLDER "Ronaldo/tests")
	add_test(NAME sc8850_submcu COMMAND sc8850_submcu_test)
	set_tests_properties(sc8850_submcu PROPERTIES TIMEOUT 20)

	add_executable(sc88_submcu_test tests/sc88_submcu_test.cpp)
	target_link_libraries(sc88_submcu_test PRIVATE 88lib)
	set_property(TARGET sc88_submcu_test PROPERTY EXCLUDE_FROM_ALL FALSE)
	set_property(TARGET sc88_submcu_test PROPERTY FOLDER "Ronaldo/tests")
	add_test(NAME sc88_submcu COMMAND sc88_submcu_test)
	add_executable(sc88_system_dt1_test tests/sc88_system_dt1_test.cpp)
	target_link_libraries(sc88_system_dt1_test PRIVATE 88lib)
	set_property(TARGET sc88_system_dt1_test PROPERTY FOLDER "Ronaldo/tests")
	add_test(NAME sc88_system_dt1 COMMAND sc88_system_dt1_test)
	set_tests_properties(sc88_system_dt1 PROPERTIES SKIP_RETURN_CODE 77)
	set_tests_properties(sc88_submcu PROPERTIES TIMEOUT 20)

endif()

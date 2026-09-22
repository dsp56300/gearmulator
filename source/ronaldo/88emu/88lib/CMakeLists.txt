cmake_minimum_required(VERSION 3.10)

project(88lib)

add_library(88lib STATIC)

set(SOURCES
	analog/analogOutput.cpp analog/analogOutput.h
	analog/analogModels.cpp analog/analogModels.h
	analog/cmVca.h
	c_interface.cpp c_interface.h
	deviceModel.cpp deviceModel.h
	boards/cm32p.cpp boards/cm32p.h
	boards/cm64.cpp boards/cm64.h
	boards/laBoard.cpp boards/laBoard.h
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

# lspLib is header-only and compiled into the boards; it brings the JIT library of the host.
target_link_libraries(88lib PUBLIC synthLib hardwareLib h8500 sh2 mcs96 xpLib lspLib gpLib lpLib rccLib la32Lib mt32ReverbLib)

target_compile_definitions(88lib PRIVATE EMU88_VERSION_STRING="${CMAKE_PROJECT_VERSION}")

target_include_directories(88lib
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# ---- lib88emu: 88lib with everything it links, as one archive
#
# What a host written in C, or built with another build system, links against: c_interface.h
# and this one file (plus the C++ runtime). Not part of ALL:
#   cmake --build <dir> --target 88emu_bundle
# The static libraries 88lib depends on are collected from its link interface, so the list
# follows the targets (asmjit is in it exactly when the host has a JIT back end).
function(emu88_collect_static_libs _target _out)
	if(NOT TARGET ${_target} OR ${_target} IN_LIST ${_out})
		return()
	endif()
	get_target_property(_type ${_target} TYPE)
	if(_type STREQUAL "STATIC_LIBRARY")
		list(APPEND ${_out} ${_target})
	endif()
	if(_type STREQUAL "INTERFACE_LIBRARY")
		get_target_property(_deps ${_target} INTERFACE_LINK_LIBRARIES)
	else()
		get_target_property(_deps ${_target} LINK_LIBRARIES)
	endif()
	foreach(_dep IN LISTS _deps)
		if(_dep AND NOT _dep MATCHES "^\\$<")
			emu88_collect_static_libs(${_dep} ${_out})
		endif()
	endforeach()
	set(${_out} ${${_out}} PARENT_SCOPE)
endfunction()

if(NOT MSVC)
	set(EMU88_BUNDLE_LIBS)
	emu88_collect_static_libs(88lib EMU88_BUNDLE_LIBS)
	set(EMU88_BUNDLE ${CMAKE_CURRENT_BINARY_DIR}/lib88emu.a)

	set(_files)
	set(_mri "create ${EMU88_BUNDLE}\n")
	foreach(_lib IN LISTS EMU88_BUNDLE_LIBS)
		list(APPEND _files $<TARGET_FILE:${_lib}>)
		string(APPEND _mri "addlib $<TARGET_FILE:${_lib}>\n")
	endforeach()
	string(APPEND _mri "save\nend\n")

	if(APPLE AND NOT EMSCRIPTEN)
		# Apple's ar has no MRI scripts; libtool merges archives.
		set(_bundle_command libtool -static -o ${EMU88_BUNDLE} ${_files})
	else()
		file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/lib88emu.mri CONTENT "${_mri}")
		set(_bundle_command sh -c "${CMAKE_AR} -M < ${CMAKE_CURRENT_BINARY_DIR}/lib88emu.mri")
	endif()

	add_custom_target(88emu_bundle
		COMMAND ${CMAKE_COMMAND} -E rm -f ${EMU88_BUNDLE}
		COMMAND ${_bundle_command}
		DEPENDS ${EMU88_BUNDLE_LIBS}
		BYPRODUCTS ${EMU88_BUNDLE}
		COMMENT "Bundling 88lib and its dependencies into lib88emu.a"
		VERBATIM)
	set_property(TARGET 88emu_bundle PROPERTY FOLDER "Ronaldo/88emu")
endif()

if(BUILD_TESTING)
	add_executable(c_interface_test tests/c_interface_test.cpp)
	target_link_libraries(c_interface_test PRIVATE 88lib)
	set_property(TARGET c_interface_test PROPERTY FOLDER "Ronaldo/tests")
	add_test(NAME c_interface COMMAND c_interface_test)
	set_tests_properties(c_interface PROPERTIES TIMEOUT 120)

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

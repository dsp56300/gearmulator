cmake_minimum_required(VERSION 3.10)

project(88lib)

add_library(88lib STATIC)

set(SOURCES
	deviceModel.cpp deviceModel.h
	hardwareDevice.cpp hardwareDevice.h
	sc88Renderer.cpp sc88Renderer.h
	rom.cpp rom.h
	romloader.cpp romloader.h
	romRegistry.h
	sc55mk2.cpp sc55mk2.h
	sc55_submcu.cpp sc55_submcu.h
	sc88.cpp sc88.h
	sc8850.cpp sc8850.h
	sc8850_submcu.cpp sc8850_submcu.h
	sc88types.h
	sc88_submcu.cpp sc88_submcu.h
	sc88pro.cpp sc88pro.h
)

target_sources(88lib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

set_property(TARGET 88lib PROPERTY FOLDER "Ronaldo/88emu")

target_link_libraries(88lib PUBLIC synthLib hardwareLib h8500 sh2 xpLib gpLib asmjit)

target_include_directories(88lib
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

if(BUILD_TESTING)
	add_executable(sc88Renderer_test sc88Renderer_test.cpp)
	target_link_libraries(sc88Renderer_test PRIVATE 88lib)
	# This directory is pulled in with EXCLUDE_FROM_ALL, which kept the test out of the generated
	# solution entirely - ctest has been reporting it as "Not Run" rather than running it.
	set_property(TARGET sc88Renderer_test PROPERTY EXCLUDE_FROM_ALL FALSE)
	set_property(TARGET sc88Renderer_test PROPERTY FOLDER "Ronaldo/tests")
	add_test(NAME sc88Renderer COMMAND sc88Renderer_test)
	set_tests_properties(sc88Renderer PROPERTIES TIMEOUT 20)
endif()

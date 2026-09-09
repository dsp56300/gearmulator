cmake_minimum_required(VERSION 3.10)

project(88lib)

add_library(88lib STATIC)

set(SOURCES
	deviceModel.cpp deviceModel.h
	hardwareDevice.cpp hardwareDevice.h
	sc88Thread.cpp sc88Thread.h
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
	add_executable(sc88Thread_test sc88Thread_test.cpp)
	target_link_libraries(sc88Thread_test PRIVATE 88lib)
	set_property(TARGET sc88Thread_test PROPERTY FOLDER "Ronaldo/tests")
	add_test(NAME sc88Thread COMMAND sc88Thread_test)
	set_tests_properties(sc88Thread PROPERTIES TIMEOUT 20)
endif()

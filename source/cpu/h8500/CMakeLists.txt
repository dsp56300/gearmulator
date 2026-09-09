cmake_minimum_required(VERSION 3.10)

project(h8500)

file(GLOB SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)

add_library(h8500 STATIC ${SOURCES})
source_group("source" FILES ${SOURCES})
set_property(TARGET h8500 PROPERTY FOLDER "cpu")

target_compile_features(h8500 PUBLIC cxx_std_20)
target_link_libraries(h8500 PUBLIC emucommon)

if(BUILD_TESTING)
	foreach(t cpu decode frt h8532 intc machine sci tmr wdt )
		add_executable(h8500_test_${t} tests/test_${t}.cpp)
		set_property(TARGET h8500_test_${t} PROPERTY FOLDER "cpu/tests")
		target_link_libraries(h8500_test_${t} PRIVATE h8500)
		target_include_directories(h8500_test_${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
		add_test(NAME h8500_${t} COMMAND h8500_test_${t})
	endforeach()
endif()

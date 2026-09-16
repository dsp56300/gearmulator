cmake_minimum_required(VERSION 3.10)

project(mcs96)

file(GLOB SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)

add_library(mcs96 STATIC ${SOURCES})
source_group("source" FILES ${SOURCES})
set_property(TARGET mcs96 PROPERTY FOLDER "cpu")

target_compile_features(mcs96 PUBLIC cxx_std_20)
target_link_libraries(mcs96 PUBLIC emucommon)

if(BUILD_TESTING)
	foreach(t cpu periph)
		add_executable(mcs96_test_${t} tests/test_${t}.cpp)
		set_property(TARGET mcs96_test_${t} PROPERTY FOLDER "cpu/tests")
		target_link_libraries(mcs96_test_${t} PRIVATE mcs96)
		add_test(NAME mcs96_${t} COMMAND mcs96_test_${t})
	endforeach()
endif()

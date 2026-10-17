cmake_minimum_required(VERSION 3.10)

project(sh3)

# Cached threaded-code Hitachi SH-3 (SH7709A) core with the on-chip
# peripheral block on the shared event scheduler. Same architecture as
# cpu/sh2 and cpu/sh4. More than one manufacturer's devices could use it,
# which is why it lives under cpu/ rather than under a single one.
file(GLOB SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)

add_library(sh3 STATIC ${SOURCES})
source_group("source" FILES ${SOURCES})
set_property(TARGET sh3 PROPERTY FOLDER "cpu")

target_compile_features(sh3 PUBLIC cxx_std_20)
target_link_libraries(sh3 PUBLIC shcore emucommon)

# Command-line disassembler for firmware analysis.
add_executable(sh3_dasm tools/sh3_dasm.cpp)
target_link_libraries(sh3_dasm PRIVATE sh3)
set_property(TARGET sh3_dasm PROPERTY FOLDER "cpu/tools")

if(BUILD_TESTING)
	foreach(t cpu periph)
		add_executable(sh3_test_${t} tests/test_${t}.cpp)
		set_property(TARGET sh3_test_${t} PROPERTY FOLDER "cpu/tests")
		target_link_libraries(sh3_test_${t} PRIVATE sh3)
		target_include_directories(sh3_test_${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
		set_property(TARGET sh3_test_${t} PROPERTY EXCLUDE_FROM_ALL FALSE)
		add_test(NAME sh3_${t} COMMAND sh3_test_${t})
	endforeach()
endif()

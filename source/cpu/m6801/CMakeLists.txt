cmake_minimum_required(VERSION 3.10)

project(m6801)

# Cached threaded-code Motorola MC6801 / Hitachi HD6301 / Motorola MC68HC11
# core (MC6803, HD6303R, MC68HC11A0) with the on-chip peripheral blocks on the
# shared event scheduler, and the MC6850 ACIA on the same scheduler. Same
# architecture as cpu/h8500.
file(GLOB SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)

add_library(m6801 STATIC ${SOURCES})
source_group("source" FILES ${SOURCES})
set_property(TARGET m6801 PROPERTY FOLDER "cpu")

target_compile_features(m6801 PUBLIC cxx_std_20)
target_link_libraries(m6801 PUBLIC emucommon)

# Firmware analysis: code-flow disassembler. Not built by default.
add_executable(m6801dasm EXCLUDE_FROM_ALL tools/m6801dasm.cpp)
target_link_libraries(m6801dasm PRIVATE m6801)
set_property(TARGET m6801dasm PROPERTY FOLDER "cpu/tools")

if(BUILD_TESTING)
	foreach(t cpu periph hc11)
		add_executable(m6801_test_${t} tests/test_${t}.cpp)
		set_property(TARGET m6801_test_${t} PROPERTY FOLDER "cpu/tests")
		target_link_libraries(m6801_test_${t} PRIVATE m6801)
		set_property(TARGET m6801_test_${t} PROPERTY EXCLUDE_FROM_ALL FALSE)
		add_test(NAME m6801_${t} COMMAND m6801_test_${t})
	endforeach()
endif()

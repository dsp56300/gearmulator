cmake_minimum_required(VERSION 3.10)

project(h8300h)

# Cached threaded-code Hitachi H8/300H (H8/3002) core with the on-chip
# peripheral block on the shared event scheduler. Same architecture as
# cpu/h8500. More than one manufacturer's devices use it, which is why it
# lives under cpu/ rather than under a single one.
file(GLOB SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)

add_library(h8300h STATIC ${SOURCES})
source_group("source" FILES ${SOURCES})
set_property(TARGET h8300h PROPERTY FOLDER "cpu")

target_compile_features(h8300h PUBLIC cxx_std_20)
target_link_libraries(h8300h PUBLIC emucommon)

# Firmware analysis: linear disassembler and control-flow walker. Not built by
# default because h8cfg needs std::filesystem, i.e. a newer macOS than the plugins.
add_executable(h8dasm EXCLUDE_FROM_ALL tools/h8dasm.cpp)
add_executable(h8cfg EXCLUDE_FROM_ALL tools/h8cfg.cpp)
foreach(t h8dasm h8cfg)
	target_link_libraries(${t} PRIVATE h8300h)
	set_property(TARGET ${t} PROPERTY FOLDER "cpu/tools")
	if(APPLE)
		target_compile_options(${t} PRIVATE -mmacosx-version-min=10.15)
		target_link_options(${t} PRIVATE -mmacosx-version-min=10.15)
	endif()
endforeach()

if(BUILD_TESTING)
	foreach(t cpu periph periph2320)
		add_executable(h8300h_test_${t} tests/test_${t}.cpp)
		set_property(TARGET h8300h_test_${t} PROPERTY FOLDER "cpu/tests")
		target_link_libraries(h8300h_test_${t} PRIVATE h8300h)
		add_test(NAME h8300h_${t} COMMAND h8300h_test_${t})
	endforeach()
endif()

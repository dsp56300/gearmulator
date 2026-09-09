cmake_minimum_required(VERSION 3.10)

project(sh2)

file(GLOB SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)

add_library(sh2 STATIC ${SOURCES})
source_group("source" FILES ${SOURCES})
set_property(TARGET sh2 PROPERTY FOLDER "cpu")

target_compile_features(sh2 PUBLIC cxx_std_20)
target_link_libraries(sh2 PUBLIC emucommon)

if(BUILD_TESTING)
	foreach(t sh2_adc sh2_bsc sh2_cmt sh2_cpu sh2_dmac sh2_intc sh2_mtu sh2_ports sh2_sci sh2_wdt sh7034 sh7042 )
		add_executable(sh2_test_${t} tests/test_${t}.cpp)
		set_property(TARGET sh2_test_${t} PROPERTY FOLDER "cpu/tests")
		target_link_libraries(sh2_test_${t} PRIVATE sh2)
		target_include_directories(sh2_test_${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
		add_test(NAME sh2_${t} COMMAND sh2_test_${t})
	endforeach()
endif()

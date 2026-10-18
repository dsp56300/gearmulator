cmake_minimum_required(VERSION 3.10)

project(hg62e33b08fLib)

# The Hitachi HG62E33B08F CPU gate array next to the 80C196KB: split instruction /
# data banking, the interrupt mux on EXTINT1 and a DMA channel.
add_library(hg62e33b08fLib STATIC hg62e33b08f.cpp hg62e33b08f.h)
set_property(TARGET hg62e33b08fLib PROPERTY FOLDER "Ronaldo")
target_include_directories(hg62e33b08fLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)

if(BUILD_TESTING)
	add_executable(hg62e33b08f_test hg62e33b08f_test.cpp)
	target_link_libraries(hg62e33b08f_test PRIVATE hg62e33b08fLib)
	set_property(TARGET hg62e33b08f_test PROPERTY FOLDER "Ronaldo/tests")
	add_test(NAME hg62e33b08f_test COMMAND hg62e33b08f_test)
endif()

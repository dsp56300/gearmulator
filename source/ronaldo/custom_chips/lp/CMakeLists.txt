cmake_minimum_required(VERSION 3.10)

project(lpLib)

# LP PCM engine.
add_library(lpLib STATIC lp.cpp lp.h ../pcmInterpolation.h)
set_property(TARGET lpLib PROPERTY FOLDER "Ronaldo")
target_include_directories(lpLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)

if(BUILD_TESTING)
	add_executable(lp_test lp_test.cpp)
	target_link_libraries(lp_test PRIVATE lpLib)
	set_property(TARGET lp_test PROPERTY FOLDER "Ronaldo/Tests")
	add_test(NAME lp_test COMMAND lp_test)
endif()

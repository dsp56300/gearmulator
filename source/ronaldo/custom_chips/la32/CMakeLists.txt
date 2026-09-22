cmake_minimum_required(VERSION 3.10)

project(la32Lib)

# LA32 partial synthesizer (R15229896 / MB87136A), the MT-32 / CM-32L tone engine. Derived from
# Nuked-MT32 (nukeykt, GPL-2.0-or-later); the notice is in la32.cpp.
add_library(la32Lib STATIC la32.cpp la32.h)
set_property(TARGET la32Lib PROPERTY FOLDER "Ronaldo")
target_include_directories(la32Lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)

if(BUILD_TESTING)
	add_executable(la32_test la32_test.cpp)
	target_link_libraries(la32_test PRIVATE la32Lib)
	set_property(TARGET la32_test PROPERTY FOLDER "Ronaldo/Tests")
	add_test(NAME la32_test COMMAND la32_test)
endif()

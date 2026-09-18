cmake_minimum_required(VERSION 3.10)

project(mt32ReverbLib)

# The Boss reverb gate array (HG61H20R36F / BOS-007) of the MT-32 and CM-32L. Its microcode
# ROM is compiled to native code once at load time (mt32reverb_jit_*.h, emitted with asmjit);
# the interpreter is the reference and the fallback.
add_library(mt32ReverbLib STATIC
	mt32reverb.cpp mt32reverb.h
	mt32reverb_common.h
	mt32reverb_interpreter.h
	mt32reverb_jit.h
	mt32reverb_jit_arm64.h
	mt32reverb_jit_x86.h
)
set_property(TARGET mt32ReverbLib PROPERTY FOLDER "Ronaldo")
target_include_directories(mt32ReverbLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(mt32ReverbLib PUBLIC asmjit)

if(BUILD_TESTING)
	add_executable(mt32reverb_test mt32reverb_test.cpp)
	target_link_libraries(mt32reverb_test PRIVATE mt32ReverbLib)
	set_property(TARGET mt32reverb_test PROPERTY FOLDER "Ronaldo/Tests")
	add_test(NAME mt32reverb_test COMMAND mt32reverb_test)
endif()

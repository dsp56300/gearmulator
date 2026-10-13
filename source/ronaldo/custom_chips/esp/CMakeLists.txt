cmake_minimum_required(VERSION 3.10)

project(esp)

# Roland ESP (TC170C140AF): interpreter, frame optimizer and native kernels (asmjit,
# arm64/x86-64). esp.cpp instantiates the ERAM sizes the boards use.
add_library(esp STATIC
	esp.cpp
	esp.h
	esp_base.h
	esp_common.h
	esp_core_program.h
	esp_interpreter.h
	esp_jit.h
	esp_jit_arm64.h
	esp_jit_backend.h
	esp_jit_emitter.h
	esp_jit_x86.h
	esp_program.h
	esp_runtime.h
	../jitCompileWorker.h
	../jitHost.h
)
target_include_directories(esp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
if(CHIPS_JIT_ASMJIT)
	target_link_libraries(esp PRIVATE asmjit)
endif()
set_property(TARGET esp PROPERTY FOLDER "Ronaldo")

add_subdirectory(tests)

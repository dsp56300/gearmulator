cmake_minimum_required(VERSION 3.15)

project(wasmJit)

# JIT support for WebAssembly builds: a minimal WebAssembly binary emitter (wasmEmitter.h) and
# the host that turns an emitted module into a callable function (wasmJitHost.h). Emulation
# code that has native JIT back ends adds a WebAssembly one on top of these.
#
# Only a JavaScript embedder can compile code at run time, so this library exists in Emscripten
# builds only (see source/CMakeLists.txt). The final link needs -sALLOW_TABLE_GROWTH.
if(NOT EMSCRIPTEN)
	message(FATAL_ERROR "wasmJit is for Emscripten builds only")
endif()

add_library(wasmJit STATIC)

set(SOURCES
	wasmEmitter.h
	wasmJitHost.cpp wasmJitHost.h
)

target_sources(wasmJit PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_include_directories(wasmJit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(wasmJit PUBLIC cxx_std_17)
# The EM_JS dependencies of wasmJitHost.cpp ($addFunction, ...) travel with the object file;
# table growth is a property of the final link, which belongs to the embedder. Tests and other
# executables of this tree get it from here.
target_link_options(wasmJit INTERFACE -sALLOW_TABLE_GROWTH=1)

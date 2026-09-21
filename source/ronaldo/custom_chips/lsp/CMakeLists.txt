cmake_minimum_required(VERSION 3.10)

project(lspLib)

# The Fujitsu MB87837 "LSP" effects DSP. Header-only: an interpreter - the reference - and the
# JIT back end of the host (../jitHost.h), native through asmjit or WebAssembly through wasmJit.
add_library(lspLib INTERFACE)
target_include_directories(lspLib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_compile_features(lspLib INTERFACE cxx_std_20)
if(CHIPS_JIT_ASMJIT)
	target_link_libraries(lspLib INTERFACE asmjit)
elseif(CHIPS_JIT_WASM)
	target_link_libraries(lspLib INTERFACE wasmJit)
endif()

set(SOURCES
	../jitHost.h
	lsp.h
	lsp_common.h
	lsp_interpreter.h
	lsp_jit.h
	lsp_jit_arm64.h
	lsp_jit_none.h
	lsp_jit_wasm.h
	lsp_jit_x86.h
	lsp_program.h
)
add_custom_target(lspLibHeaders SOURCES ${SOURCES})
set_property(TARGET lspLibHeaders PROPERTY FOLDER "Ronaldo")

cmake_minimum_required(VERSION 3.15)

add_subdirectory(common)

if(${CMAKE_PROJECT_NAME}_SYNTH_JE8086)
	add_subdirectory(esp)
	add_subdirectory(je8086)
endif()

if(${CMAKE_PROJECT_NAME}_SYNTH_88EMU)
	# The custom-chip JITs have a back end on some hosts only; custom_chips/jitHost.h decides.
	# Ask the header instead of second-guessing it, so asmjit is linked exactly when the code
	# uses it and a host without a back end (32-bit, RISC-V, WebAssembly, ...) builds without.
	option(CHIPS_FORCE_NO_JIT "Run the custom-chip DSPs on their interpreters even where a JIT back end exists" OFF)
	if(CHIPS_FORCE_NO_JIT)
		add_compile_definitions(CHIPS_FORCE_NO_JIT=1)
		set(CHIPS_JIT_ASMJIT FALSE)
	else()
		include(CheckCXXSourceCompiles)
		set(CMAKE_REQUIRED_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/custom_chips)
		check_cxx_source_compiles("
			#include \"jitHost.h\"
			#if !CHIPS_JIT_ASMJIT
			#error no asmjit back end on this host
			#endif
			int main() { return 0; }" CHIPS_JIT_ASMJIT)
		unset(CMAKE_REQUIRED_INCLUDES)
	endif()
	message(STATUS "Custom-chip JIT through asmjit: ${CHIPS_JIT_ASMJIT}")
	# The WebAssembly back ends (CHIPS_JIT_WASM in jitHost.h) are built on framework/wasmJit,
	# which exists in Emscripten builds only.
	if(TARGET wasmJit AND NOT CHIPS_FORCE_NO_JIT)
		set(CHIPS_JIT_WASM TRUE)
	else()
		set(CHIPS_JIT_WASM FALSE)
	endif()
	message(STATUS "Custom-chip JIT through wasmJit: ${CHIPS_JIT_WASM}")

	add_subdirectory(custom_chips/lsp)
	add_subdirectory(custom_chips/xp)
	add_subdirectory(custom_chips/gp)
	add_subdirectory(custom_chips/lp)
	add_subdirectory(custom_chips/rcc)
	add_subdirectory(custom_chips/mt32reverb)
	add_subdirectory(88emu)
endif()

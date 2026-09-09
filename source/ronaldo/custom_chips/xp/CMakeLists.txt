cmake_minimum_required(VERSION 3.10)

project(xpLib)

add_library(xpLib STATIC)

set(SOURCES
	xp_dsp.cpp xp_dsp.h
	xp_dsp_state.h
	xp_dsp_ops.h
	xp_dsp_program.cpp xp_dsp_program.h
	xp_dsp_naive.cpp
	xp_dsp_jit.h
	xp_dsp_jit_arm64.h
	xp_dsp_jit_x86.h
	xp.cpp xp.h
	xp_host.cpp
	xp_voice_common.h
	xp_voice.cpp
)

target_sources(xpLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

set_property(TARGET xpLib PROPERTY FOLDER "Ronaldo")

target_include_directories(xpLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# The DSP JIT backends (xp_dsp_jit_*.h) are emitted with asmjit.
target_link_libraries(xpLib PUBLIC asmjit)

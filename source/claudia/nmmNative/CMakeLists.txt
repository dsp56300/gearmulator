cmake_minimum_required(VERSION 3.20)
project(nmmNative LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)

get_filename_component(NMM_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(NMM_EMULATOR_BUILD "${NMM_REPO_ROOT}/build/nmm" CACHE PATH "Configured current emulator build")
option(NMM_BUILD_REFERENCE "Build desktop firmware capture tools" ON)
add_library(nmmNativeCore STATIC core/nmm101_kernels.cpp core/nmm101_oscillator.cpp core/nmm101_oscillator_setup.cpp core/nmm101_envelope.cpp core/nmm101_filter_control.cpp core/nmm101_graph.cpp)
target_include_directories(nmmNativeCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/core")
target_sources(nmmNativeCore PRIVATE core/nmm101_dac.cpp)

if(NMM_BUILD_REFERENCE)
    if(NOT EXISTS "${NMM_EMULATOR_BUILD}/CMakeCache.txt")
        message(FATAL_ERROR "Configure the emulator first, or set NMM_BUILD_REFERENCE=OFF. See README.md.")
    endif()
    # Refresh the existing reference without changing its configuration or source.
    add_custom_target(nmmRefreshReference
        COMMAND "${CMAKE_COMMAND}" --build "${NMM_EMULATOR_BUILD}" --target nmmLib
        COMMENT "Rebuilding the current emulator reference")
    add_library(nmmReference INTERFACE)
    target_include_directories(nmmReference INTERFACE
        "${NMM_REPO_ROOT}/source/claudia/nmm"
        "${NMM_REPO_ROOT}/source/framework"
        "${NMM_REPO_ROOT}/source/cpu"
        "${NMM_REPO_ROOT}/source/3rdparty"
        "${NMM_REPO_ROOT}/source/cpu/dsp56300/source"
        "${NMM_REPO_ROOT}/source/cpu/dsp56300/source/asmjit/src")
    target_compile_definitions(nmmReference INTERFACE DSP56300_DEBUGGER=0 RELEASE ASMJIT_STATIC)
    set(reference_archives
        source/claudia/nmm/nmmLib/libnmmLib.a
        source/framework/hardwareLib/libhardwareLib.a
        source/cpu/mc68k/lib68kEmu.a
        source/framework/synthLib/libsynthLib.a
        source/3rdparty/libresample/libresample.a
        source/framework/baseLib/libbaseLib.a
        source/cpu/dsp56300/source/dsp56kEmu/libdsp56kEmu.a
        source/cpu/dsp56300/source/dsp56kBase/libdsp56kBase.a
        source/cpu/dsp56300/source/asmjit/libasmjit.a)
    foreach(archive IN LISTS reference_archives)
        target_link_libraries(nmmReference INTERFACE "${NMM_EMULATOR_BUILD}/${archive}")
    endforeach()
    find_package(Threads REQUIRED)
    target_link_libraries(nmmReference INTERFACE Threads::Threads ${CMAKE_DL_LIBS})
    add_executable(nmm101Capture reference/nmm101Capture.cpp)
    target_link_libraries(nmm101Capture PRIVATE nmmReference)
    add_dependencies(nmm101Capture nmmRefreshReference)
    add_executable(nmmFilterF92Differential reference/nmmFilterF92Differential.cpp)
    target_link_libraries(nmmFilterF92Differential PRIVATE nmmReference nmmNativeCore)
    add_dependencies(nmmFilterF92Differential nmmRefreshReference)
    add_test(NAME nmmFilterF92Differential COMMAND nmmFilterF92Differential)
    add_executable(nmmFilterF92ControlDifferential reference/nmmFilterF92ControlDifferential.cpp)
    target_link_libraries(nmmFilterF92ControlDifferential PRIVATE nmmReference nmmNativeCore)
    add_dependencies(nmmFilterF92ControlDifferential nmmRefreshReference)
    add_test(NAME nmmFilterF92ControlDifferential COMMAND nmmFilterF92ControlDifferential)
    add_test(NAME nmmFilterF92ControlJitDifferential COMMAND nmmFilterF92ControlDifferential --jit)
    add_executable(nmmOscillatorSetupDifferential reference/nmmOscillatorSetupDifferential.cpp)
    target_link_libraries(nmmOscillatorSetupDifferential PRIVATE nmmReference nmmNativeCore)
    add_dependencies(nmmOscillatorSetupDifferential nmmRefreshReference)
    add_test(NAME nmmOscillatorSetupDifferential COMMAND nmmOscillatorSetupDifferential)
    add_test(NAME nmmOscillatorSetupJitDifferential COMMAND nmmOscillatorSetupDifferential --jit)
    add_executable(nmmOscillatorDifferential reference/nmmOscillatorDifferential.cpp)
    target_link_libraries(nmmOscillatorDifferential PRIVATE nmmReference nmmNativeCore)
    add_dependencies(nmmOscillatorDifferential nmmRefreshReference)
    add_test(NAME nmmOscillatorDifferential COMMAND nmmOscillatorDifferential)
    add_test(NAME nmmOscillatorJitDifferential COMMAND nmmOscillatorDifferential --jit)
    add_executable(nmmGraphDifferential reference/nmmGraphDifferential.cpp)
    target_link_libraries(nmmGraphDifferential PRIVATE nmmReference nmmNativeCore)
    add_dependencies(nmmGraphDifferential nmmRefreshReference)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/reference-a/warmup_dsp_p.bin")
        add_test(NAME nmmGraphDifferential COMMAND nmmGraphDifferential "${CMAKE_CURRENT_SOURCE_DIR}/build/reference-a")
        add_test(NAME nmmGraphJitDifferential COMMAND nmmGraphDifferential "${CMAKE_CURRENT_SOURCE_DIR}/build/reference-a" --jit)
    endif()
    add_executable(nmmOutputDifferential reference/nmmOutputDifferential.cpp)
    target_link_libraries(nmmOutputDifferential PRIVATE nmmReference nmmNativeCore)
    add_dependencies(nmmOutputDifferential nmmRefreshReference)
    add_test(NAME nmmOutputDifferential COMMAND nmmOutputDifferential)
    add_test(NAME nmmOutputJitDifferential COMMAND nmmOutputDifferential --jit)
    add_executable(nmmFilterOutputDifferential reference/nmmFilterOutputDifferential.cpp)
    target_link_libraries(nmmFilterOutputDifferential PRIVATE nmmReference nmmNativeCore)
    add_dependencies(nmmFilterOutputDifferential nmmRefreshReference)
    add_test(NAME nmmFilterOutputDifferential COMMAND nmmFilterOutputDifferential)
    add_test(NAME nmmFilterOutputJitDifferential COMMAND nmmFilterOutputDifferential --jit)
    add_executable(nmmEnvelopeDifferential reference/nmmEnvelopeDifferential.cpp)
    target_link_libraries(nmmEnvelopeDifferential PRIVATE nmmReference nmmNativeCore)
    add_dependencies(nmmEnvelopeDifferential nmmRefreshReference)
    add_test(NAME nmmEnvelopeDifferential COMMAND nmmEnvelopeDifferential)
    add_test(NAME nmmEnvelopeJitDifferential COMMAND nmmEnvelopeDifferential --jit)
    add_executable(nmmDacDifferential reference/nmmDacDifferential.cpp)
    target_link_libraries(nmmDacDifferential PRIVATE nmmReference nmmNativeCore)
    add_dependencies(nmmDacDifferential nmmRefreshReference)
    add_test(NAME nmmDacDifferential COMMAND nmmDacDifferential)
    add_test(NAME nmmDacJitDifferential COMMAND nmmDacDifferential --jit)
    add_executable(nmmGraphCapture reference/nmmGraphCapture.cpp)
    target_link_libraries(nmmGraphCapture PRIVATE nmmReference)
    add_dependencies(nmmGraphCapture nmmRefreshReference)
endif()

add_executable(nmmFixedTests core/dsp56300_fixed_test.cpp)
add_test(NAME nmmFixedTests COMMAND nmmFixedTests)
add_executable(nmmKernelTests core/nmm101_kernels_test.cpp)
target_link_libraries(nmmKernelTests PRIVATE nmmNativeCore)
add_test(NAME nmmKernelTests COMMAND nmmKernelTests)
foreach(test_target nmmFixedTests nmmKernelTests)
    target_compile_options(${test_target} PRIVATE -UNDEBUG)
endforeach()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME nmmCaptureComparatorTests
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_diff_capture.py")
add_executable(nmmMcuContractTests mcu/nord101_mcu_contract_test.cpp)
target_compile_options(nmmMcuContractTests PRIVATE -UNDEBUG)
add_test(NAME nmmMcuContractTests COMMAND nmmMcuContractTests)
add_executable(nmmMcuTraceTests mcu/nord101_mcu_trace_test.cpp)
add_test(NAME nmmMcuTraceTests COMMAND nmmMcuTraceTests)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/fixtures/graph101_vectors.h"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/fixtures/filter_f92_vectors.h"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/reference-a/nord101_reference_full_state.generated.h")
    add_executable(nmmBoardOracleReplay reference/nmmBoardOracleReplay.cpp)
    target_include_directories(nmmBoardOracleReplay PRIVATE reference build/fixtures build/reference-a)
    target_link_libraries(nmmBoardOracleReplay PRIVATE nmmNativeCore)
    add_test(NAME nmmBoardOracleReplay COMMAND nmmBoardOracleReplay)
endif()

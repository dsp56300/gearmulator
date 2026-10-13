# ESP core regression: hardware corpus replay on the interpreter, plus the
# native engine held to the interpreter's state.
add_executable(esp_test esp_test_main.cpp)
set_property(TARGET esp_test PROPERTY FOLDER "Ronaldo")
target_link_libraries(esp_test PRIVATE esp)
target_include_directories(esp_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
	# the fixture tables exceed the default section limit of an object file
	target_compile_options(esp_test PRIVATE /bigobj)
endif()

add_test(NAME esp_test COMMAND esp_test)
if(CHIPS_JIT_ASMJIT)
	target_link_libraries(esp_test PRIVATE asmjit)
endif()

# Synthetic whole-core state parity on both native backends. No ROM fixtures required.
add_executable(esp_whole_core_test esp_whole_core_test.cpp)
target_link_libraries(esp_whole_core_test PRIVATE esp)
set_property(TARGET esp_whole_core_test PROPERTY FOLDER "Ronaldo")
add_test(NAME esp_whole_core_test COMMAND esp_whole_core_test)
set_tests_properties(esp_whole_core_test PROPERTIES SKIP_RETURN_CODE 77)
if(CHIPS_JIT_ASMJIT)
	target_link_libraries(esp_whole_core_test PRIVATE asmjit)
endif()

# The compile handshake shared by every custom-chip JIT dispatcher. lsp, added before this directory when the
# 88emu is enabled, builds the same test.
if(NOT TARGET jitCompileWorkerTest)
	add_executable(jitCompileWorkerTest ../../jitCompileWorker_test.cpp)
	target_link_libraries(jitCompileWorkerTest PRIVATE esp)
	set_property(TARGET jitCompileWorkerTest PROPERTY FOLDER "Ronaldo")
	add_test(NAME jitCompileWorkerTest COMMAND jitCompileWorkerTest)
endif()

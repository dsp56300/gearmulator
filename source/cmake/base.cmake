if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Plugins and tools built into the source tree go here, and the build runs some of them (VST3 manifest, LV2 .ttl,
# changelogs). Both change for a Windows ARM64 build, see below.
set(TUS_BIN_DIR "${CMAKE_SOURCE_DIR}/bin")
set(TUS_CAN_RUN_BUILT_BINARIES ON)

if(MSVC)
	# https://cmake.org/cmake/help/latest/variable/CMAKE_MSVC_RUNTIME_LIBRARY.html#variable:CMAKE_MSVC_RUNTIME_LIBRARY
	cmake_policy(SET CMP0091 NEW)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS} /IGNORE:4221")

	# /O2 Full Optimization (Favor Speed)
	# /GS- disable security checks
	# /fp:fast
	# /Oy omit frame pointers
	# /GT enable fiber-safe optimizations
	# /GL Whole Program Optimization
	# /Zi Generate Debug Info PDB
	# /Oi Enable Intrinsic Functions
	# /Ot Favor Fast Code
	# /permissive- Standards Conformance
	# /MP Multiprocessor Compilation

	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /O2 /GS- /fp:fast /Oy /GT /GL /Zi /Oi /Ot")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /GS- /fp:fast /Oy /GT /GL /Zi /Oi /Ot")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /permissive- /MP")

	set(ARCHITECTURE ${CMAKE_VS_PLATFORM_NAME})

	# 32 bit x86 only, x64 has SSE2 anyway and ARM64 does not know the option
	if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "X86")
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /arch:SSE2")
	endif()

	if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64")
		# usually built next to an x64 build of the same tree, whose plugins and tools it would overwrite
		set(TUS_BIN_DIR "${CMAKE_SOURCE_DIR}/bin/arm64")

		# arm64_neon.h, which <intrin.h> pulls in, otherwise defines short macros such as mvn(src) that rewrite
		# every asmjit Assembler::mvn() call in the JITs into neon_not()
		add_compile_definitions(_ARM64_NO_EXTENDED_INTRINSICS)

		# An x64 machine cannot run what this builds. CMake does not call that cross compiling, as the system
		# name stays the same, so it is up to us to skip the steps that run a freshly built binary.
		if(NOT CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "ARM64")
			set(TUS_CAN_RUN_BUILT_BINARIES OFF)
			message(WARNING "ARM64 cross build: no LV2, no VST3 moduleinfo.json, no changelog generation, and the tests cannot run here")
		endif()
	endif()

	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /D_CRT_SECURE_NO_WARNINGS")

	set(CMAKE_STATIC_LINKER_FLAGS_RELEASE "${CMAKE_STATIC_LINKER_FLAGS_RELEASE} /LTCG")
	set(CMAKE_MODULE_LINKER_FLAGS_RELEASE "${CMAKE_MODULE_LINKER_FLAGS_RELEASE} /LTCG /DEBUG")
	set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_MODULE_LINKER_FLAGS_RELEASE} /LTCG /DEBUG")
	set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG /DEBUG")

	set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS /SAFESEH:NO")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SAFESEH:NO")
elseif(APPLE)
#	set(ARCHITECTURE ${CMAKE_OSX_ARCHITECTURES})
	set(ARCHITECTURE "MacOS")
	set(OS_LINK_LIBRARIES
	    "-framework Accelerate"
	    "-framework ApplicationServices"
	    "-framework AudioUnit"
	    "-framework AudioToolbox"
	    "-framework Carbon"
	    "-framework CoreAudio"
	    "-framework CoreAudioKit"
	    "-framework CoreServices"
	    "-framework CoreText"
	    "-framework Cocoa"
	    "-framework CoreFoundation"
	    "-framework OpenGL"
	    "-framework QuartzCore"  	
	)
	string(APPEND CMAKE_C_FLAGS_RELEASE " -funroll-loops -Ofast -flto -fno-stack-protector")
	string(APPEND CMAKE_CXX_FLAGS_RELEASE " -funroll-loops -Ofast -flto -fno-stack-protector")

	# Ship a dSYM for Release so crashes in released macOS builds can be symbolized (a tester's Live crash
	# report could only be read as raw offsets because no dSYM existed). The -g below is what makes the Xcode
	# generator turn on GCC_GENERATE_DEBUGGING_SYMBOLS - it stays off for Release otherwise, and setting that
	# attribute directly is a no-op because CMake overrides it per target. dwarf-with-dsym then makes Xcode run
	# dsymutil to emit a <product>.dSYM whose UUID matches the binary. The shipped binary keeps the same
	# optimized code; the debug info lives only in the .dSYM. Release only.
	#
	# Gate -g to the Xcode generator: it is the only generator that emits the dSYM, and the one every shipped
	# macOS build goes through (self-hosted M2 via scripts/generate.cmake). Makefile/Ninja builds - the
	# GitHub-hosted Nightly/CMake smoke tests on stock macos-14 runners - get NO dSYM from -g, only inline
	# DWARF that, under -flto, balloons each arm64 plugin-bundle link from seconds to ~30 min and busts
	# GitHub's hard 6h job limit (every Nightly since 2026-07-30 was cancelled at 6h for exactly this).
	if(CMAKE_GENERATOR STREQUAL "Xcode")
		string(APPEND CMAKE_C_FLAGS_RELEASE " -g")
		string(APPEND CMAKE_CXX_FLAGS_RELEASE " -g")
	endif()
	set(CMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT[variant=Release] "dwarf-with-dsym")
else()
	message("CMAKE_SYSTEM_PROCESSOR: " ${CMAKE_SYSTEM_PROCESSOR})
	message("CMAKE_HOST_SYSTEM_PROCESSOR: " ${CMAKE_HOST_SYSTEM_PROCESSOR})

	# x86 only. The Emscripten toolchain reports "x86" as its processor, but -msse there means the
	# SSE-on-wasm-SIMD emulation and needs -msimd128; an embedder asks for that itself.
	if(NOT EMSCRIPTEN AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86|x86_64|amd64|AMD64|i[3-6]86)$")
		string(APPEND CMAKE_CXX_FLAGS " -msse")
	endif()

	# GCC still has LTO issues
	if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		message(WARNING "LTO disabled due to GCC detected which is causing issues")
	else()
		cmake_policy(SET CMP0069 NEW)
		include(CheckIPOSupported)

		check_ipo_supported(RESULT result)
		if(result)
			message(STATUS "IPO is supported")
			set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
		else()
			message(WARNING "IPO is not supported")
		endif()
	endif()

	string(APPEND CMAKE_C_FLAGS_RELEASE " -Ofast -fno-stack-protector")
	string(APPEND CMAKE_CXX_FLAGS_RELEASE " -Ofast -fno-stack-protector")
	string(APPEND CMAKE_CXX_FLAGS_DEBUG " -rdynamic")

	# Link the C++ runtime statically so that shipped binaries do not require the
	# libstdc++ of the machine they happened to be built on. Without this a binary
	# built with the toolchain PPA demands a GLIBCXX version that even current
	# distributions do not ship, for example GLIBCXX_3.4.30 needs gcc 12 and so
	# fails on Ubuntu 22.04.
	# Set globally on purpose: this used to be repeated per target and had already
	# been forgotten for xtTestConsole, n2xTestConsole and the DSP bridge.
	add_link_options(-static-libgcc -static-libstdc++)

	execute_process(COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE ARCHITECTURE)

	# Good atomics are important on aarch64, they exist on ARMv8.1a or higher
	# Check some known common machines and tell compiler if present
	execute_process(COMMAND uname -a COMMAND tr -d '\n' OUTPUT_VARIABLE UNAME_A)
	if(
		UNAME_A MATCHES rk3588 		# Orange Pi 5 variants
		OR
		UNAME_A MATCHES rock-5b		# Raxda Rock 5B
		OR
		UNAME_A MATCHES rpi-2712	# Raspberry Pi 5
		)
		string(APPEND CMAKE_CXX_FLAGS " -march=armv8.2-a")
		string(APPEND CMAKE_C_FLAGS " -march=armv8.2-a")
	endif()
endif()

message( STATUS "Architecture: ${ARCHITECTURE}" )
message( STATUS "Compiler Arguments: ${CMAKE_CXX_FLAGS}" )
message( STATUS "Compiler Arguments (Release): ${CMAKE_CXX_FLAGS_RELEASE}" )
message( STATUS "Compiler Arguments (Debug): ${CMAKE_CXX_FLAGS_DEBUG}" )
message( STATUS "Build Configration: ${CMAKE_BUILD_TYPE}" )

# VST3 SDK needs these
# Generator expressions, not CMAKE_BUILD_TYPE: multi-config generators (Visual Studio,
# Xcode) ignore it, so testing it gave their Debug configuration NDEBUG and no asserts.
# $<CONFIG:Debug> is the same test JUCE uses for its DEBUG/NDEBUG definitions.
# NDEBUG on every target, not just the JUCE-linked ones: build types that do
# not inject it (None, or unset) otherwise disagree on JUCE_DEBUG and sizeof
add_compile_definitions(
	$<IF:$<CONFIG:Debug>,_DEBUG,RELEASE>
	$<$<NOT:$<CONFIG:Debug>>:NDEBUG>)

# we need C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(UNIX AND NOT APPLE)
	set(CMAKE_POSITION_INDEPENDENT_CODE ON)
	set(CMAKE_CXX_VISIBILITY_PRESET hidden)
	set(CMAKE_C_VISIBILITY_PRESET hidden)
	set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
endif()

set(PA_DISABLE_INSTALL ON)
set(PA_BUILD_SHARED OFF)

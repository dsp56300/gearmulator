if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
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

	if(NOT ${CMAKE_VS_PLATFORM_NAME} STREQUAL "x64")
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /arch:SSE2")
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

    if(NOT ${PROJECT_NAME}_ENABLE_LTO)
		message(WARNING "LTO disabled due to requested configuration")
    endif()

	string(APPEND CMAKE_C_FLAGS_RELEASE " -O3 -ffast-math -fno-stack-protector")
	string(APPEND CMAKE_CXX_FLAGS_RELEASE " -O3 -ffast-math -fno-stack-protector")

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

	if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES arm AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES aarch64)
		string(APPEND CMAKE_CXX_FLAGS " -msse")
	endif()

    if(NOT ${PROJECT_NAME}_ENABLE_LTO)
		message(WARNING "LTO disabled due to requested configuration")
	# GCC still has LTO issues
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
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

	string(APPEND CMAKE_C_FLAGS_RELEASE " -O3 -ffast-math -fno-stack-protector")
	string(APPEND CMAKE_CXX_FLAGS_RELEASE " -O3 -ffast-math -fno-stack-protector")
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
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	add_definitions(/D_DEBUG)
else()
	add_definitions(/DRELEASE)
endif()

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

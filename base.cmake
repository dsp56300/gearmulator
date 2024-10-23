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
else()
	message("CMAKE_SYSTEM_PROCESSOR: " ${CMAKE_SYSTEM_PROCESSOR})
	message("CMAKE_HOST_SYSTEM_PROCESSOR: " ${CMAKE_HOST_SYSTEM_PROCESSOR})

	if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES arm AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES aarch64)
		string(APPEND CMAKE_CXX_FLAGS " -msse")
	endif()

	# GCC <= 11 has LTO issues
	if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS_EQUAL 11.4)
		message(WARNING "LTO disabled due to GCC version <= 11.4.0 causing issues")
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
endif()

set(PA_DISABLE_INSTALL ON)
set(PA_BUILD_SHARED OFF)

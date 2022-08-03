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

	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /GS- /fp:fast /Oy /GT /GL /Zi /Oi /Ot")

	set(ARCHITECTURE ${CMAKE_VS_PLATFORM_NAME})

	if(NOT ${CMAKE_VS_PLATFORM_NAME} STREQUAL "x64")
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /arch:SSE2")
	endif()

	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /D_CRT_SECURE_NO_WARNINGS")

	set(CMAKE_STATIC_LINKER_FLAGS_RELEASE "${CMAKE_STATIC_LINKER_FLAGS_RELEASE} /LTCG")
	set(CMAKE_MODULE_LINKER_FLAGS_RELEASE "${CMAKE_MODULE_LINKER_FLAGS_RELEASE} /LTCG /DEBUG")
	set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")

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
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -funroll-loops -Ofast -flto")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -funroll-loops -Ofast -flto")
else()
	message("CMAKE_SYSTEM_PROCESSOR: " ${CMAKE_SYSTEM_PROCESSOR})
	message("CMAKE_HOST_SYSTEM_PROCESSOR: " ${CMAKE_HOST_SYSTEM_PROCESSOR})
	if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES arm AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES aarch64)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse")
	endif()
	set(CMAKE_C_FLAGS_RELEASE "-Ofast")
	set(CMAKE_CXX_FLAGS_RELEASE "-Ofast")
	set(CMAKE_CXX_FLAGS "-Ofast")
	execute_process(COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE ARCHITECTURE)
endif()

message( STATUS "Architecture: ${ARCHITECTURE}" )

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

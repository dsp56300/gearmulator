# disable freetype deps, not really needed
set(FT_DISABLE_ZLIB TRUE)
set(FT_DISABLE_BZIP2 TRUE)
set(FT_DISABLE_PNG TRUE)
set(FT_DISABLE_HARFBUZZ TRUE)
set(FT_DISABLE_BROTLI TRUE)

# we don't want to install anything from freetype
set(SKIP_INSTALL_HEADERS)
set(SKIP_INSTALL_LIBRARIES)
set(SKIP_INSTALL_ALL)

add_subdirectory(freetype)
if(CMAKE_C_COMPILER_ID MATCHES "AppleClang|Clang")
    target_compile_options(freetype PRIVATE -Wno-shorten-64-to-32)
    # Bundled zlib defines OS_CODE in overlapping macOS platform branches.
    target_compile_options(freetype PRIVATE -Wno-macro-redefined)
endif()

# RmlUi expects Freetype::Freetype instead of freetype
add_library(Freetype::Freetype ALIAS freetype)

# Lua 5.4 for RmlUi scripting
add_subdirectory(lua)
if(MSVC)
	# Lua's GC size expressions intentionally shift 32-bit values before they
	# participate in 64-bit arithmetic. Keep C4334 local to the vendor target.
	target_compile_options(lua PRIVATE /wd4334)
endif()

# Enable RmlUi Lua bindings and point to our bundled Lua
set(RMLUI_LUA_BINDINGS ON CACHE BOOL "" FORCE)
set(RMLUI_LUA_BINDINGS_LIBRARY "lua_as_cxx" CACHE STRING "" FORCE)

# Create the Lua::Lua target so RmlUi's Dependencies.cmake finds it and skips find_package(Lua)
add_library(Lua::Lua INTERFACE IMPORTED)
set_target_properties(Lua::Lua PROPERTIES
	INTERFACE_LINK_LIBRARIES lua
	INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/lua"
)

# LunaSVG (+ bundled PlutoVG) for the RmlUi SVG plugin. Provides the
# lunasvg::lunasvg target so RmlUi's Dependencies.cmake skips find_package
set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PLUTOVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(lunasvg)
if(MSVC)
	# Suppress known narrowing conversions only while compiling LunaSVG itself.
	target_compile_options(lunasvg PRIVATE /wd4244 /wd4267)
endif()

# Enable the RmlUi SVG plugin, it is compiled into rmlui_core
set(RMLUI_SVG_PLUGIN ON CACHE BOOL "" FORCE)

# RmlUi supplies its own /W4 warning level. Temporarily remove the parent
# project's /W3 so MSVC does not report warning D9025 for every RmlUi source.
if(MSVC)
	set(GEARMULATOR_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
	string(REPLACE " /W3" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

add_subdirectory(RmlUi)

# RmlUi's private, non-placement operator new stubs assert and return nullptr;
# layout objects are allocated through its pools instead. Keep this vendor
# diagnostic suppression local to rmlui_core, without changing its sources.
if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
	target_compile_options(rmlui_core PRIVATE -Wno-new-returns-null)
	# Some Lua binding overrides do not use their CoreInstance argument.
	if(TARGET rmlui_lua)
		target_compile_options(rmlui_lua PRIVATE -Wno-unused-parameter)
	endif()
endif()
if(MSVC AND TARGET rmlui_lua)
	# Match the unused-parameter suppression above for MSVC-style drivers.
	target_compile_options(rmlui_lua PRIVATE /wd4100)
endif()

# Restore CMAKE_CXX_FLAGS from the temporary saved copy.
# Remove the temporary variable used for configuring RmlUi.
if(MSVC)
	set(CMAKE_CXX_FLAGS "${GEARMULATOR_CXX_FLAGS}")
	unset(GEARMULATOR_CXX_FLAGS)
endif()

target_include_directories(rmlui_core PUBLIC 
	${CMAKE_CURRENT_LIST_DIR}/RmlUi/Include
	${CMAKE_CURRENT_LIST_DIR}/RmlUi/Source
	)

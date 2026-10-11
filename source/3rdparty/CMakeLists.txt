add_subdirectory(libresample)

# disable freetype deps, not really needed
set(FT_DISABLE_ZLIB TRUE)
set(FT_DISABLE_BZIP2 TRUE)
set(FT_DISABLE_PNG TRUE)
set(FT_DISABLE_HARFBUZZ TRUE)
set(FT_DISABLE_BROTLI TRUE)

# we don't want to install anything from freetype
set(SKIP_INSTALL_ALL TRUE)

add_subdirectory(freetype)

# RmlUi expects Freetype::Freetype instead of freetype
add_library(Freetype::Freetype ALIAS freetype)

# Lua 5.4 for RmlUi scripting
add_subdirectory(lua)

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

# Enable the RmlUi SVG plugin, it is compiled into rmlui_core
set(RMLUI_SVG_PLUGIN ON CACHE BOOL "" FORCE)

# finally...
add_subdirectory(RmlUi)

target_include_directories(rmlui_core PUBLIC 
	${CMAKE_CURRENT_LIST_DIR}/RmlUi/Include
	${CMAKE_CURRENT_LIST_DIR}/RmlUi/Source
	)

# network bridge
add_subdirectory(ptypes EXCLUDE_FROM_ALL)

# Host audio and MIDI for the test consoles and the player library. Excluded from ALL, they are built only for a
# target that links them; a WebAssembly build has no host to talk to
if(NOT EMSCRIPTEN)
	set(CPPTERMINAL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(CPPTERMINAL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
	set(CPPTERMINAL_ENABLE_TESING OFF CACHE BOOL "" FORCE)
	add_subdirectory(cpp-terminal EXCLUDE_FROM_ALL)

	if(NOT ANDROID)
		add_subdirectory(portmidi EXCLUDE_FROM_ALL)
	endif()

	set(PA_USE_ASIO OFF CACHE BOOL "" FORCE)
	add_subdirectory(portaudio EXCLUDE_FROM_ALL)

	# Virtual MIDI ports require the newer PortMidi API. Its target name differs from
	# Vavra's portmidi-static, so both versions can coexist.
	# Only the player library uses them, so a library-only build (no JUCE) does not pull in the
	# host MIDI stack.
	if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN AND NOT ANDROID AND NOT TARGET portmidi)
		# Build static even if a parent set BUILD_SHARED_LIBS ON.
		set(_pm_prev_shared ${BUILD_SHARED_LIBS})
		set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
		add_subdirectory(portmidi-latest EXCLUDE_FROM_ALL)
		set(BUILD_SHARED_LIBS ${_pm_prev_shared} CACHE BOOL "" FORCE)
	endif()
endif()

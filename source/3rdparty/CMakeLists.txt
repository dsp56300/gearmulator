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

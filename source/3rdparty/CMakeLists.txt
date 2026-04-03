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
set(LUA_LIBRARIES lua)
set(LUA_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lua)
set(LUA_FOUND TRUE)

# finally...
add_subdirectory(RmlUi)

target_include_directories(rmlui_core PUBLIC 
	${CMAKE_CURRENT_LIST_DIR}/RmlUi/Include
	${CMAKE_CURRENT_LIST_DIR}/RmlUi/Source
	)

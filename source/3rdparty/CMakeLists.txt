# disable freetype deps, not really needed
set(FT_DISABLE_ZLIB=TRUE)
set(FT_DISABLE_BZIP2=TRUE)
set(FT_DISABLE_PNG=TRUE)
set(FT_DISABLE_HARFBUZZ=TRUE)
set(FT_DISABLE_BROTLI=TRUE)

# we don't want to install anything from freetype
set(SKIP_INSTALL_HEADERS)
set(SKIP_INSTALL_LIBRARIES)
set(SKIP_INSTALL_ALL)

add_subdirectory(freetype)

# RmlUi expects Freetype::Freetype instead of freetype
add_library(Freetype::Freetype ALIAS freetype)

# finally...
add_subdirectory(RmlUi)

target_include_directories(rmlui_core PUBLIC 
	${CMAKE_CURRENT_LIST_DIR}/RmlUi/Include
	${CMAKE_CURRENT_LIST_DIR}/RmlUi/Source
	)

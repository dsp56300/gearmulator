cmake_minimum_required(VERSION 3.15)
project(lua LANGUAGES CXX)

# Lua 5.4.7 - built as a static library (compiled as C++ for safe exception handling in C++ hosts)
# lua.c and luac.c are the standalone interpreter and compiler, excluded from the library

file(GLOB LUA_SOURCES "*.c")
list(REMOVE_ITEM LUA_SOURCES
	"${CMAKE_CURRENT_SOURCE_DIR}/lua.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/luac.c"
)

add_library(lua STATIC ${LUA_SOURCES})
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)

target_include_directories(lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(UNIX)
	target_compile_definitions(lua PRIVATE LUA_USE_POSIX)
	target_link_libraries(lua PRIVATE m)
endif()

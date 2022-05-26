cmake_minimum_required(VERSION 3.16)

# Set the project name
project(68kEmu)
  
# Add sub directories
add_library(68kEmu STATIC
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Moira/Moira.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Moira/MoiraDebugger.cpp
)

# Add include paths
target_include_directories(68kEmu PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Moira
)

set_property(TARGET 68kEmu PROPERTY CXX_STANDARD 20)

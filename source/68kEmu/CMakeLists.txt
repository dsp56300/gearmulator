cmake_minimum_required(VERSION 3.16)

# Set the project name
project(68kEmu)
  
# Add sub directories
add_library(68kEmu STATIC)

set(SOURCES_MOIRA
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Moira/Moira.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Moira/Moira.h
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Moira/MoiraALU.h
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Moira/MoiraALU_cpp.h
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Moira/MoiraConfig.h
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Moira/MoiraDasm.h
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Moira/MoiraDasm_cpp.h
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Moira/MoiraDataflow.h
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Moira/MoiraDataflow_cpp.h
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Moira/MoiraDebugger.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Moira/MoiraDebugger.h
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Moira/MoiraExceptions.h
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Moira/MoiraExceptions_cpp.h
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Moira/MoiraExec_cpp.h
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Moira/MoiraInit.h
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Moira/MoiraInit_cpp.h
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Moira/MoiraTypes.h
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Moira/StrWriter.h
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Moira/StrWriter_cpp.h
)

set(SOURCES
	gpt.cpp gpt.h
	mc68k.cpp mc68k.h
	peripheralBase.cpp peripheralBase.h
	peripheralTypes.h
	qsm.cpp qsm.h
	qspi.cpp qspi.h
	sim.cpp sim.h
)

target_sources(68kEmu PRIVATE ${SOURCES} ${SOURCES_MOIRA})
source_group("source" FILES ${SOURCES})
source_group("source\\Moira" FILES ${SOURCES_MOIRA})

# Add include paths
target_include_directories(68kEmu PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Moira
)

set_property(TARGET 68kEmu PROPERTY CXX_STANDARD 20)

target_link_libraries(68kEmu PUBLIC dsp56kEmu)

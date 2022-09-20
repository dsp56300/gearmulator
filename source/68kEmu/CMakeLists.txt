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

set(SOURCES_MUSASHI
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Musashi/m68k.h
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Musashi/m68kconf.h
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Musashi/m68kcpu.c
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Musashi/m68kcpu.h
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Musashi/m68kdasm.c
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Musashi/m68kfpu.c
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Musashi/m68kmmu.h
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Musashi/m68kops.c
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Musashi/m68kops.h
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Musashi/softfloat/mamesf.h
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Musashi/softfloat/milieu.h
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Musashi/softfloat/softfloat.c
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Musashi/softfloat/softfloat.h
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Musashi/softfloat/softfloat-macros
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Musashi/softfloat/softfloat-specialize
)

set(SOURCES
	gpt.cpp gpt.h
	hdi08.cpp hdi08.h
	logging.cpp logging.h
	mc68k.cpp mc68k.h
	peripheralBase.cpp peripheralBase.h
	peripheralTypes.h
	port.cpp port.h
	qsm.cpp qsm.h
	qspi.cpp qspi.h
	sim.cpp sim.h
)

target_sources(68kEmu PRIVATE ${SOURCES} ${SOURCES_MUSASHI})
source_group("source" FILES ${SOURCES})
source_group("source\\Moira" FILES ${SOURCES_MOIRA})
source_group("source\\Musashi" FILES ${SOURCES_MUSASHI})

# Add include paths
target_include_directories(68kEmu PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/Moira/Moira
)

set_property(TARGET 68kEmu PROPERTY CXX_STANDARD 17)

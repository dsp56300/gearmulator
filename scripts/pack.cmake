include(${CMAKE_CURRENT_LIST_DIR}/io.cmake)

message(STATUS "Removing old packages")

removeGlob("*.zip")
removeGlob("*.deb")
removeGlob("*.rpm")

macro(pack GENERATOR)
	message(STATUS "Packaging ${GENERATOR}")
	set(PACK_RESULT 0)
	execute_process(COMMAND cpack -G ${GENERATOR} 
		COMMAND_ECHO STDOUT
		RESULT_VARIABLE PACK_RESULT)
	if(PACK_RESULT)
		message(FATAL_ERROR "Failed to execute cpack: " ${PACK_RESULT})
	endif()
endmacro()

pack("ZIP")

if(UNIX AND NOT APPLE)
	pack("DEB")
	pack("RPM")
endif()

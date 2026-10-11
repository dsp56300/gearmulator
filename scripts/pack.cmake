include(${CMAKE_CURRENT_LIST_DIR}/io.cmake)

message(STATUS "Removing old packages")

removeGlob("*.zip")
removeGlob("*.deb")
removeGlob("*.rpm")

# cpack stages a complete copy of everything it is about to package below _CPack_Packages and leaves it
# behind, on success as well as on failure: about 3 GB per generator for a full product set, so the three
# generators together left ~9 GB lying in the build directory. That outgrew the 63 GB Linux build machines -
# the aarch64 one filled its disk in the middle of Pack, which also knocked the Jenkins agent offline. Each
# generator stages from scratch anyway, so clearing it before every run costs nothing, keeps the peak at one
# generator instead of three, and takes a previous failed run's leftovers with it.
macro(removePackStaging)
	if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/_CPack_Packages")
		message(STATUS "Removing cpack staging directory")
		file(REMOVE_RECURSE "${CMAKE_CURRENT_BINARY_DIR}/_CPack_Packages")
	endif()
endmacro()

macro(pack GENERATOR)
	message(STATUS "Packaging ${GENERATOR}")
	removePackStaging()
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

# the packages are what the following stages upload, the staging copy of them is not
removePackStaging()

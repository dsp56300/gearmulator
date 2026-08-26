message(STATUS "Testing AU Validation for Company ID ${IDCOMPANY}, Plugin ID ${IDPLUGIN}")
message(STATUS "cmake binary dir ${CMAKE_BINARY_DIR}")
message(STATUS "cmake current binary dir ${CMAKE_CURRENT_BINARY_DIR}")
message(STATUS "cmake current source dir ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "BINDIR ${BINDIR}")
message(STATUS "CPACK_FILE ${CPACK_FILE}")

set(SOURCE_DIR ${BINDIR})
set(TARGET_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/Components")

set(SOURCE_ZIP ${SOURCE_DIR}/${CPACK_FILE})
set(TARGET_ZIP ${TARGET_DIR}/${CPACK_FILE})

set(TARGET_COMPONENT ${TARGET_DIR}/${COMPONENT_NAME}.component)

message(STATUS "Copying AU plugin to ${TARGET_DIR}")

if(EXISTS ${TARGET_COMPONENT})
	execute_process(COMMAND rm -R ${TARGET_COMPONENT} COMMAND_ECHO STDOUT RESULT_VARIABLE REMOVE_RESULT)
	if(REMOVE_RESULT)
		message(FATAL_ERROR "Failed to remove ${TARGET_COMPONENT}")
	endif()
endif()

execute_process(COMMAND unzip -o ${SOURCE_ZIP} -d ${TARGET_DIR} COMMAND_ECHO STDOUT RESULT_VARIABLE UNZIP_RESULT)
if(UNZIP_RESULT)
	message(FATAL_ERROR "Failed to unzip ${TARGET_ZIP}")
endif()

# MacOS needs some time to register the new plugin, attempt to sleep but if the validation fails try again up to 3 times

foreach(i RANGE 3)
	execute_process(COMMAND sleep 5 COMMAND_ECHO STDOUT RESULT_VARIABLE SLEEP_RESULT)
	if(SLEEP_RESULT)
		message(FATAL_ERROR "Failed to sleep")
	endif()

	execute_process(COMMAND auvaltool -v aumu ${IDPLUGIN} ${IDCOMPANY} COMMAND_ECHO STDOUT RESULT_VARIABLE AUVALTOOL_RESULT)
	if(AUVALTOOL_RESULT EQUAL 0)
		message(STATUS "AU validation succeeded at attempt ${i}")
		break()
	else()
		if(i LESS 3)
			message(WARNING "AU validation failed at attempt ${i}, retrying...")
		else()
			message(FATAL_ERROR "AU validation failed")
		endif()
	endif()
endforeach()

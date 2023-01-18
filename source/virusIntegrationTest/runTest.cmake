include(${CMAKE_CURRENT_LIST_DIR}/../../scripts/rclone.cmake)

set(TEST_DATA_DIR integrationTestsData)

if(EXISTS ${RCLONE_CONF})
	copyDataFrom("integrationtests" ${TEST_DATA_DIR})

	execute_process(COMMAND ${TEST_RUNNER} -folder ${TEST_DATA_DIR} COMMAND_ECHO STDOUT RESULT_VARIABLE TEST_RESULT)
	if(TEST_RESULT)
		message(FATAL_ERROR "Failed to execute ${TEST_RUNNER}: " ${CMD_RESULT})
	endif()
else()
	message(FATAL_ERROR "rclone.conf not found at ${RCLONE_CONF}, unable to run integration tests")
endif()

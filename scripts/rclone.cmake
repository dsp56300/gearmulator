if(NOT ROOT_DIR)
	set(ROOT_DIR ${CMAKE_BINARY_DIR})
endif()

if(NOT RCLONE_CONF)
	if(DEFINED $ENV{RCLONE_CONF})
		set(RCLONE_CONF $ENV{RCLONE_CONF})
	else()
		set(RCLONE_CONF ${ROOT_DIR}/rclone.conf)
	endif()
endif()

macro(copyArtefacts TARGET FOLDER)
	set(RCLONE_RESULT 0)
	execute_process(COMMAND rclone --config ${RCLONE_CONF} copy 
		--transfers=1 -v --include *.zip --include *.deb --include *.rpm --min-size 100 --max-depth 1 "${ROOT_DIR}" "${TARGET}/${FOLDER}"
		COMMAND_ECHO STDOUT
		RESULT_VARIABLE RCLONE_RESULT
		WORKING_DIRECTORY ${ROOT_DIR})
	if(RCLONE_RESULT)
		message(FATAL_ERROR "Failed to execute rclone: " ${CMD_RESULT})
	endif()
endmacro()

macro(copyDataFrom FROM TO)
	execute_process(COMMAND rclone --config "${RCLONE_CONF}" sync "dsp56300:${FROM}" "${TO}" COMMAND_ECHO STDOUT RESULT_VARIABLE RCLONE_RESULT)
	if(RCLONE_RESULT)
		message(FATAL_ERROR "Failed to execute rclone: " ${CMD_RESULT})
	endif()
endmacro()

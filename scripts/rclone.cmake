if(NOT ROOT_DIR)
	set(ROOT_DIR ${CMAKE_BINARY_DIR})
endif()

if(NOT RCLONE_CONF)
	if(DEFINED ENV{RCLONE_CONF})
		set(RCLONE_CONF $ENV{RCLONE_CONF})
	else()
		set(RCLONE_CONF ${ROOT_DIR}/rclone.conf)
	endif()
endif()

set(postfix "_partial")

macro(copyArtefacts TARGET FOLDER)
	set(RCLONE_RESULT 0)
	file(GLOB files LIST_DIRECTORIES false "${ROOT_DIR}/*.zip" "${ROOT_DIR}/*.deb" "${ROOT_DIR}/*.rpm")
	foreach(fileLocal ${files})
		
		file(SIZE ${fileLocal} fileSize)
		if(${fileSize} GREATER 100)
			file(RELATIVE_PATH remoteFile "${ROOT_DIR}" ${fileLocal})

			file(RELATIVE_PATH remoteFile "${ROOT_DIR}" ${fileLocal})
			set(remoteFileTemp "${remoteFile}${postfix}")

			set(RCLONE_RESULT 0)
			execute_process(COMMAND rclone -v --config ${RCLONE_CONF} copyto 
				"${fileLocal}" "${TARGET}/${FOLDER}/${remoteFileTemp}"
				COMMAND_ECHO STDOUT RESULT_VARIABLE RCLONE_RESULT WORKING_DIRECTORY ${ROOT_DIR})
			if(RCLONE_RESULT)
				message(FATAL_ERROR "Failed to execute rclone: " ${CMD_RESULT})
			endif()

			set(RCLONE_RESULT 0)
			execute_process(COMMAND rclone -v --config ${RCLONE_CONF} moveto 
				"${TARGET}/${FOLDER}/${remoteFileTemp}" "${TARGET}/${FOLDER}/${remoteFile}"
				COMMAND_ECHO STDOUT RESULT_VARIABLE RCLONE_RESULT WORKING_DIRECTORY ${ROOT_DIR})
			if(RCLONE_RESULT)
				message(FATAL_ERROR "Failed to execute rclone: " ${CMD_RESULT})
			endif()
		endif()
	endforeach()
endmacro()

macro(copyDataFrom FROM TO)
	execute_process(COMMAND rclone --config "${RCLONE_CONF}" sync "dsp56300:${FROM}" "${TO}" COMMAND_ECHO STDOUT RESULT_VARIABLE RCLONE_RESULT)
	if(RCLONE_RESULT)
		message(FATAL_ERROR "Failed to execute rclone: " ${CMD_RESULT})
	endif()
endmacro()

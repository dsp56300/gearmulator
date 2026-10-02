if(NOT ROOT_DIR)
	set(ROOT_DIR ${CMAKE_BINARY_DIR})
	if(NOT ROOT_DIR)
		set(ROOT_DIR ${gearmulator_BINARY_DIR})
		if(NOT ROOT_DIR)
			message(FATAL_ERROR "Unable to determine binary directory")
		endif()
	endif()
endif()

if(NOT RCLONE_CONF)
	if(DEFINED ENV{RCLONE_CONF})
		set(RCLONE_CONF $ENV{RCLONE_CONF})
	else()
		set(RCLONE_CONF ${ROOT_DIR}/rclone.conf)
	endif()
endif()

# A tier folder on the web server is readable only with its own .htaccess + .htpasswd, and rclone
# creates missing folders as it uploads. Deploying into a tier that was never set up therefore
# publishes the builds with no password at all: that is how builds/88emuplayer/internal came to
# serve the internal 2.2.26 set to anyone for seven days. Refuse the upload instead of creating an
# open folder.
macro(requireAccessControl TARGET FOLDER)
	execute_process(COMMAND rclone --config ${RCLONE_CONF} lsf "${TARGET}/${FOLDER}/"
		OUTPUT_VARIABLE AC_LISTING ERROR_VARIABLE AC_STDERR RESULT_VARIABLE AC_RESULT)

	if(NOT AC_RESULT EQUAL 0)
		message(FATAL_ERROR "Refusing to upload to ${TARGET}/${FOLDER}/ : the folder does not exist, "
			"and the upload would create it with no password protection. Create it first, with the "
			"same .htaccess and .htpasswd that the other tiers have. rclone said: ${AC_STDERR}")
	endif()

	if(NOT AC_LISTING MATCHES "(^|\n)\\.htaccess")
		message(FATAL_ERROR "Refusing to upload to ${TARGET}/${FOLDER}/ : it has no .htaccess, so the "
			"builds would be served to anyone without a password. Add the .htaccess and .htpasswd "
			"that the other tiers have, then deploy again.")
	endif()
endmacro()

macro(copyArtefacts TARGET FOLDER FILEFILTER)
	set(RCLONE_RESULT 0)
	execute_process(COMMAND rclone --transfers 5 -v --config ${RCLONE_CONF} copy --include "/*${FILEFILTER}*.{zip,deb,rpm}" --min-size 8k ${ROOT_DIR}/ "${TARGET}/${FOLDER}/"
		COMMAND_ECHO STDOUT RESULT_VARIABLE RCLONE_RESULT WORKING_DIRECTORY ${ROOT_DIR})
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

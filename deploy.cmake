include(scripts/rclone.cmake)

set(RCLONE_CONF ${ROOT_DIR}/rclone.conf)

macro(copyArtefacts TARGET BRANCH FOLDER)
	set(RCLONE_RESULT 0)
	execute_process(COMMAND rclone --config ${RCLONE_CONF} copy 
		"--transfers=1 -v --include *.zip --include *.deb --include *.rpm --min-size 100 --max-depth 1 . ${TARGET}/${BRANCH}${FOLDER}"
		COMMAND_ECHO STDOUT RESULT_VARIABLE RCLONE_RESULT WORKING_DIRECTORY ${ROOT_DIR})
	if(RCLONE_RESULT)
		message(FATAL_ERROR "Failed to execute rclone: " ${CMD_RESULT})
	endif()
endmacro()

if(EXISTS ${RCLONE_CONF})
	copyArtefacts("dsp56300:deploy")
	copyArtefacts("dsp56300_ftp:builds")
else()
	message(WARNING "rclone.conf not found, unable to deploy/upload")
endif()

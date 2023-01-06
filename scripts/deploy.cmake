include(${CMAKE_CURRENT_LIST_DIR}/rclone.cmake)

execute_process(COMMAND git rev-parse --abbrev-ref HEAD OUTPUT_VARIABLE BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NOT BRANCH)
	message(FATAL_ERROR "unable to determine current git branch")
endif()

if(NOT FOLDER)
	message(FATAL_ERROR "no upload folder specified")
endif()

if(NOT UPLOAD_LOCAL AND NOT UPLOAD_REMOTE)
	message(FATAL_ERROR "neither upload to local nor remote is set")
endif()

if(EXISTS ${RCLONE_CONF})
	if(UPLOAD_LOCAL)
		copyArtefacts("dsp56300:deploy" ${BRANCH} ${FOLDER})
	endif()
	if(UPLOAD_REMOTE)
		copyArtefacts("dsp56300_ftp:builds" ${BRANCH} ${FOLDER})
	endif()
else()
	message(FATAL_ERROR "rclone.conf not found, unable to deploy/upload")
endif()

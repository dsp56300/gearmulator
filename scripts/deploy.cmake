include(${CMAKE_CURRENT_LIST_DIR}/rclone.cmake)

if(NOT FOLDER)
	message(FATAL_ERROR "no upload folder specified")
endif()

if(NOT UPLOAD_LOCAL AND NOT UPLOAD_REMOTE)
	message(FATAL_ERROR "neither upload to local nor remote is set")
endif()

if(NOT EXISTS ${RCLONE_CONF})
	message(FATAL_ERROR "rclone.conf not found, unable to deploy/upload")
endif()

if(UPLOAD_LOCAL)
	copyArtefacts("dsp56300:deploy" "${FOLDER}" "${FILTER}")
endif()
if(UPLOAD_REMOTE)
	copyArtefacts("dsp56300_sftp:builds" "${FOLDER}" "${FILTER}")
endif()

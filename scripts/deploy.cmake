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
	# FOLDER is either "<product>/<tier>" or just "<product>/". Only a tier is password protected, so
	# only a tier has to prove it before anything is uploaded into it. The product root is not
	# checked: it is not meant to be readable at all, the product's own rule answers 404 there.
	if(FOLDER MATCHES "^[^/]+/[^/]+")
		requireAccessControl("dsp56300_upload:builds" "${FOLDER}")
	endif()
	copyArtefacts("dsp56300_upload:builds" "${FOLDER}" "${FILTER}")
endif()

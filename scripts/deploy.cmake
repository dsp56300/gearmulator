include(${CMAKE_CURRENT_LIST_DIR}/rclone.cmake)

execute_process(COMMAND git rev-parse --abbrev-ref HEAD OUTPUT_VARIABLE BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NOT BRANCH)
	message(FATAL_ERROR "unable to determine current git branch")
endif()

if(EXISTS ${RCLONE_CONF})
	copyArtefacts("dsp56300:deploy" ${BRANCH} "test")
	copyArtefacts("dsp56300_ftp:builds" ${BRANCH} "test")
else()
	message(WARNING "rclone.conf not found, unable to deploy/upload")
endif()

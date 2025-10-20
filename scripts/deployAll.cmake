if(NOT FOLDER)
	set(FOLDER "")
endif()

if(NOT gearmulator_BINARY_DIR)
	message(FATAL_ERROR "Source of binaries to be uploaded 'gearmulator_BINARY_DIR' not specified")
endif()

include(${gearmulator_BINARY_DIR}/CPackConfig.cmake)

macro(deploySynth NAME)
	message(STATUS "Deploying product ${NAME}")
	# upload folder is the lowercase version of the name
	string(TOLOWER ${NAME} folder)
	set(uploadFolder ${folder}/${FOLDER})
	set(filter "${NAME}")
	execute_process(COMMAND cmake -DFOLDER=${uploadFolder} -DFILTER=${filter} -DUPLOAD_LOCAL=${UPLOAD_LOCAL} -DUPLOAD_REMOTE=${UPLOAD_REMOTE} -P ${CMAKE_CURRENT_LIST_DIR}/deploy.cmake COMMAND_ECHO STDOUT WORKING_DIRECTORY ${gearmulator_BINARY_DIR} COMMAND_ERROR_IS_FATAL ANY)
endmacro()

foreach(S IN LISTS CPACK_TUS_TARGETS)
	message("Processing target " ${S})
	deploySynth(${CPACK_TUS_${S}_PRODUCT_NAME})
endforeach()

include(${CMAKE_CURRENT_LIST_DIR}/synths.cmake)

if(NOT FOLDER)
	message(FATAL_ERROR "No upload folder specified")
endif()

if(NOT gearmulator_BINARY_DIR)
	message(FATAL_ERROR "Source of binaries to be uploaded 'gearmulator_BINARY_DIR' not specified")
endif()

macro(deploySynth NAME)
	message(STATUS "Deploying synth ${${NAME}_name}")
	set(uploadFolder ${FOLDER}/${${NAME}_folder})
	set(filter "${${NAME}_name}")
	execute_process(COMMAND cmake -DFOLDER=${uploadFolder} -DFILTER=${filter} -DUPLOAD_LOCAL=${UPLOAD_LOCAL} -DUPLOAD_REMOTE=${UPLOAD_REMOTE} -P ${CMAKE_CURRENT_LIST_DIR}/deploy.cmake COMMAND_ECHO STDOUT WORKING_DIRECTORY ${gearmulator_BINARY_DIR} COMMAND_ERROR_IS_FATAL ANY)
endmacro()

foreach(S IN LISTS synths)
	if(${S})
		deploySynth(${S})
	endif()
endforeach()

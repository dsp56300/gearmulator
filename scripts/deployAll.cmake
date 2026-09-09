if(NOT FOLDER)
	set(FOLDER "")
endif()

if(NOT gearmulator_BINARY_DIR)
	message(FATAL_ERROR "Source of binaries to be uploaded 'gearmulator_BINARY_DIR' not specified")
endif()

include(${gearmulator_BINARY_DIR}/CPackConfig.cmake)

# FOLDER may name more than one destination tier, joined with '+', for example
# "internal+donators". '+' rather than ';' so the value survives
# Jenkins -> shell -> cmake without quoting. An empty FOLDER still means the product root.
set(uploadTiers "")
if(FOLDER)
	string(REPLACE "+" ";" uploadTiers "${FOLDER}")
endif()

macro(deployTier UPLOADFOLDER FILTER)
	execute_process(COMMAND cmake -DFOLDER=${UPLOADFOLDER} -DFILTER=${FILTER} -DUPLOAD_LOCAL=${UPLOAD_LOCAL} -DUPLOAD_REMOTE=${UPLOAD_REMOTE} -P ${CMAKE_CURRENT_LIST_DIR}/deploy.cmake COMMAND_ECHO STDOUT WORKING_DIRECTORY ${gearmulator_BINARY_DIR} COMMAND_ERROR_IS_FATAL ANY)
endmacro()

macro(deploySynth NAME)
	message(STATUS "Deploying product ${NAME}")
	# upload folder is the lowercase version of the name
	string(TOLOWER ${NAME} folder)
	set(filter "${NAME}")
	if(uploadTiers)
		foreach(tier IN LISTS uploadTiers)
			deployTier("${folder}/${tier}" "${filter}")
		endforeach()
	else()
		deployTier("${folder}/" "${filter}")
	endif()
endmacro()

foreach(S IN LISTS CPACK_TUS_TARGETS)
	message("Processing target " ${S})
	deploySynth(${CPACK_TUS_${S}_PRODUCT_NAME})
endforeach()

#file(GLOB sourceZips LIST_DIRECTORIES false "${gearmulator_BINARY_DIR}/*-Source.zip")
#if(sourceZips)
#	deploySynth("Source")
#endif()

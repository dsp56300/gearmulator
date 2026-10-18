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

macro(deployTier UPLOADFOLDER FILTER CHANGELOG)
	execute_process(COMMAND cmake -DFOLDER=${UPLOADFOLDER} -DFILTER=${FILTER} "-DCHANGELOG=${CHANGELOG}" -DUPLOAD_LOCAL=${UPLOAD_LOCAL} -DUPLOAD_REMOTE=${UPLOAD_REMOTE} -P ${CMAKE_CURRENT_LIST_DIR}/deploy.cmake COMMAND_ECHO STDOUT WORKING_DIRECTORY ${gearmulator_BINARY_DIR} COMMAND_ERROR_IS_FATAL ANY)
endmacro()

# The product's own changelog, which the Plugin Manager reads from the folder its builds are in. The
# one changelogGenerator wrote for it rather than doc/changelog.txt: a folder for one product has no
# business telling about the others, and on a device branch an unreleased one may be among them. It is
# the same file every archive of the product carries anyway.
#
# Matched without case or spaces, because the changelog heads it "DSP Bridge" where the build says
# "DSPBridge". Empty when there is none, and then the folder gets none.
function(findChangelog NAME OUT)
	string(REPLACE " " "" wanted "${NAME}")
	string(TOLOWER "${wanted}" wanted)

	file(GLOB logs LIST_DIRECTORIES false "${CPACK_TUS_SOURCE_DIR}/doc/changelog_split/changelog_*.txt")

	set(${OUT} "" PARENT_SCOPE)

	foreach(log IN LISTS logs)
		get_filename_component(name "${log}" NAME_WE)
		string(REGEX REPLACE "^changelog_" "" name "${name}")
		string(REPLACE " " "" name "${name}")
		string(TOLOWER "${name}" name)

		if(name STREQUAL wanted)
			set(${OUT} "${log}" PARENT_SCOPE)
			return()
		endif()
	endforeach()
endfunction()

macro(deploySynth NAME)
	message(STATUS "Deploying product ${NAME}")
	# upload folder is the lowercase version of the name
	string(TOLOWER ${NAME} folder)
	set(filter "${NAME}")
	findChangelog("${NAME}" changelog)
	if(NOT changelog)
		message(STATUS "No changelog for ${NAME} in ${CPACK_TUS_SOURCE_DIR}/doc/changelog_split")
	endif()
	if(uploadTiers)
		foreach(tier IN LISTS uploadTiers)
			deployTier("${folder}/${tier}" "${filter}" "${changelog}")
		endforeach()
	else()
		deployTier("${folder}/" "${filter}" "${changelog}")
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

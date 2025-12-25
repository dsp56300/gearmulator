if(NOT gearmulator_SOURCE_DIR)
	message(FATAL_ERROR "Source directory 'gearmulator_SOURCE_DIR' not specified")
endif()

if(NOT gearmulator_BINARY_DIR)
	message(FATAL_ERROR "Source of binaries to be uploaded 'gearmulator_BINARY_DIR' not specified")
endif()

include(${gearmulator_BINARY_DIR}/CPackConfig.cmake)

if(NOT CPACK_PACKAGE_VERSION)
	message(FATAL_ERROR "Unable to determine project version")
endif()
message("Project Version: " ${CPACK_PACKAGE_VERSION})

# ------------------------------

file(GLOB filesToUpload
    LIST_DIRECTORIES false
    "${gearmulator_BINARY_DIR}/*${CPACK_PACKAGE_VERSION}*.zip"
    "${gearmulator_BINARY_DIR}/*${CPACK_PACKAGE_VERSION}*.deb"
    "${gearmulator_BINARY_DIR}/*${CPACK_PACKAGE_VERSION}*.rpm"
)

set(FILTERED_FILES "")

foreach(file ${filesToUpload})
    get_filename_component(fname "${file}" NAME)

    if(fname MATCHES "Source|pkgconfig|headers")
        message(STATUS "Skipping ${fname}")
    else()
        list(APPEND FILTERED_FILES "${file}")
    endif()
endforeach()

list(LENGTH FILTERED_FILES fileCount)

if(${fileCount} LESS 1)
	message(FATAL_ERROR "No files found to be uploaded")
endif()

message("Files to upload:  ${FILTERED_FILES}")

# ------------------------------

message("Generating Changelog")
if(WIN32)
	set(changelogApp changelogGenerator.exe)
else()
	set(changelogApp changelogGenerator)
endif()

execute_process(COMMAND ${gearmulator_SOURCE_DIR}/bin/tools/${changelogApp} -i ${gearmulator_SOURCE_DIR}/doc/changelog.txt -o ${gearmulator_SOURCE_DIR}/doc/changelog_split COMMAND_ERROR_IS_FATAL ANY)

# ------------------------------

message("Checking if release ${CPACK_PACKAGE_VERSION} already exists")

if(NOT DEFINED ENV{GH_TOKEN})
	set(ENV{GH_TOKEN} ${GH_TOKEN})
endif()

set(GH_RESULT -1)
execute_process(COMMAND gh release view ${CPACK_PACKAGE_VERSION} --repo dsp56300/gearmulator RESULT_VARIABLE GH_RESULT)

if(${GH_RESULT} EQUAL 1)
	execute_process(COMMAND gh release create ${CPACK_PACKAGE_VERSION} --repo dsp56300/gearmulator --draft --title "${CPACK_PACKAGE_VERSION}" --notes-file ${gearmulator_SOURCE_DIR}/doc/changelog_split/${CPACK_PACKAGE_VERSION}.txt COMMAND_ERROR_IS_FATAL ANY)
elseif(${GH_RESULT} EQUAL 0)
	message("Release ${CPACK_PACKAGE_VERSION} already exists")
else()
	message(FATAL_ERROR "Unknown error code ${GH_RESULT}")
endif()

execute_process(
    COMMAND gh release upload ${CPACK_PACKAGE_VERSION} ${FILTERED_FILES} --repo dsp56300/gearmulator --clobber
    RESULT_VARIABLE GH_RESULT
    OUTPUT_VARIABLE GH_OUT
    ERROR_VARIABLE  GH_ERR
)

if(NOT GH_RESULT EQUAL 0)
    message(FATAL_ERROR "GitHub release upload failed: ${GH_ERR}")
endif()

message("Upload to GitHub finished")

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED gearmulator_BINARY_DIR)
	message(FATAL_ERROR "binary directory is not set")
endif()

include("${gearmulator_BINARY_DIR}/CPackSourceConfig.cmake")

if(NOT DEFINED CPACK_TUS_SOURCE_DIR)
	message(FATAL_ERROR "Source directory not defined")
endif()

set(OUTPUT_DIR "${gearmulator_BINARY_DIR}/_src")

message(STATUS "Cleaning directory ${OUTPUT_DIR}")

file(REMOVE_RECURSE "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

message(STATUS "Cloning via git from ${CPACK_TUS_SOURCE_DIR} into ${OUTPUT_DIR}")

execute_process(
    COMMAND git clone --recurse-submodules "${CPACK_TUS_SOURCE_DIR}" "${OUTPUT_DIR}"
    WORKING_DIRECTORY "${CPACK_TUS_SOURCE_DIR}"
	COMMAND_ERROR_IS_FATAL ANY
)

message(STATUS "Removing .git directory")

file(REMOVE_RECURSE "${OUTPUT_DIR}/.git")

set(ARCHIVE_NAME "${CPACK_PACKAGE_FILE_NAME}.zip")

message(STATUS "Creating source archive ${ARCHIVE_NAME}")

execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar "cf" "${CPACK_PACKAGE_FILE_NAME}.zip" --format=zip ${OUTPUT_DIR}
    WORKING_DIRECTORY "${gearmulator_BINARY_DIR}"
	COMMAND_ERROR_IS_FATAL ANY
)

message(STATUS "Source archive ${ARCHIVE_NAME} created")

message(STATUS "Removing directory ${OUTPUT_DIR}")
file(REMOVE_RECURSE "${OUTPUT_DIR}")

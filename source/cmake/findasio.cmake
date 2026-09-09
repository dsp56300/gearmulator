# Steinberg's ASIO SDK may not be redistributed, so unlike the rest of 3rdparty it is not in
# the tree, and JUCE only builds its ASIO backend when the SDK's common/iasiodrv.h is on the
# include path (see juce.cmake). In order of preference:
#   1. an explicit -D${CMAKE_PROJECT_NAME}_ASIO_SDK_PATH,
#   2. an extracted copy in source/3rdparty/asiosdk,
#   3. Steinberg's own download, fetched into the build directory - every CI build starts
#      from a clean checkout and no agent carries the SDK, so this is what CI ends up using.
# Fetching the archive means accepting Steinberg's licence terms for it. A failed download
# (no network, changed URL) only leaves ASIO disabled; it never fails the configure.

set(ASIOSDK_URL "https://www.steinberg.net/asiosdk")

if(WIN32 AND NOT ${CMAKE_PROJECT_NAME}_ASIO_SDK_PATH)
	set(ASIOSDK_INTREE ${CMAKE_CURRENT_LIST_DIR}/../3rdparty/asiosdk)
	set(ASIOSDK_DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/asiosdk)

	if(EXISTS ${ASIOSDK_INTREE}/common/iasiodrv.h)
		set(${CMAKE_PROJECT_NAME}_ASIO_SDK_PATH ${ASIOSDK_INTREE})
	else()
		# the archive extracts to a versioned folder, asiosdk_2.3.3_2019-06-14 and the like
		file(GLOB ASIOSDK_HEADER ${ASIOSDK_DOWNLOAD_DIR}/*/common/iasiodrv.h)

		if(NOT ASIOSDK_HEADER)
			message(STATUS "Downloading the ASIO SDK from ${ASIOSDK_URL}")
			file(MAKE_DIRECTORY ${ASIOSDK_DOWNLOAD_DIR})
			set(ASIOSDK_ARCHIVE ${ASIOSDK_DOWNLOAD_DIR}/asiosdk.zip)
			file(DOWNLOAD ${ASIOSDK_URL} ${ASIOSDK_ARCHIVE} STATUS ASIOSDK_STATUS TIMEOUT 120 TLS_VERIFY ON)
			list(GET ASIOSDK_STATUS 0 ASIOSDK_RESULT)
			if(ASIOSDK_RESULT EQUAL 0)
				# The archive carries logo artwork with non-ASCII names, which libarchive refuses
				# to extract under the C locale and then stops. Only common/ matters, and the
				# glob below decides whether we got it, so a partial extraction is not an error.
				execute_process(COMMAND ${CMAKE_COMMAND} -E env LC_ALL=C.UTF-8 ${CMAKE_COMMAND} -E tar xf ${ASIOSDK_ARCHIVE}
					WORKING_DIRECTORY ${ASIOSDK_DOWNLOAD_DIR} RESULT_VARIABLE ASIOSDK_RESULT OUTPUT_QUIET ERROR_QUIET)
			else()
				list(GET ASIOSDK_STATUS 1 ASIOSDK_ERROR)
				message(STATUS "Unable to download the ASIO SDK: ${ASIOSDK_ERROR}")
			endif()
			file(REMOVE ${ASIOSDK_ARCHIVE})
			file(GLOB ASIOSDK_HEADER ${ASIOSDK_DOWNLOAD_DIR}/*/common/iasiodrv.h)
		endif()

		if(ASIOSDK_HEADER)
			list(GET ASIOSDK_HEADER 0 ASIOSDK_HEADER)
			get_filename_component(ASIOSDK_COMMON ${ASIOSDK_HEADER} DIRECTORY)
			get_filename_component(ASIOSDK_ROOT ${ASIOSDK_COMMON} DIRECTORY)
			set(${CMAKE_PROJECT_NAME}_ASIO_SDK_PATH ${ASIOSDK_ROOT})
		endif()
	endif()

	if(NOT ${CMAKE_PROJECT_NAME}_ASIO_SDK_PATH)
		message(STATUS "ASIO SDK not available, the standalones will offer DirectSound and WASAPI only")
	endif()
endif()

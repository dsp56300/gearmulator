cmake_minimum_required(VERSION 3.15)

# build a fat binary that runs on both intel and the new Apple M1 chip
if(APPLE)
	include(xcodeversion.cmake)

	set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "OS X Architectures")

	message("CMAKE_GENERATOR: " ${CMAKE_GENERATOR})
	message("XCODE_VERSION: " ${XCODE_VERSION})

	# Xcode 14+ can not build for anything < High Sierra anymore
	if(CMAKE_GENERATOR STREQUAL Xcode AND XCODE_VERSION VERSION_GREATER_EQUAL 14.0.0)
		set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum OS X deployment version")
	else()
		# 10.12 is now the minimum because we use std::shared_mutex in patch manager
		set(CMAKE_OSX_DEPLOYMENT_TARGET "10.12" CACHE STRING "Minimum OS X deployment version")
	endif()
	message("CMAKE_OSX_DEPLOYMENT_TARGET: " ${CMAKE_OSX_DEPLOYMENT_TARGET})
endif()

project(gearmulator VERSION 2.1.0)

include(base.cmake)
include(CTest)

option(${PROJECT_NAME}_BUILD_JUCEPLUGIN "Build Juce plugin" on)
option(${PROJECT_NAME}_BUILD_JUCEPLUGIN_CLAP "Build CLAP version of Juce plugin" on)

# ----------------- CPack basic parameters

message("CMAKE_SYSTEM_NAME: " ${CMAKE_SYSTEM_NAME})
message("CMAKE_SYSTEM_PROCESSOR: " ${CMAKE_SYSTEM_PROCESSOR})

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	set(CPACK_SYSTEM_NAME ${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR})
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
	set(CPACK_SYSTEM_NAME "MacOS")
endif()

if(NOT DEFINED CPACK_SYSTEM_NAME)
	if(WIN32)
		if(CMAKE_SIZEOF_VOID_P EQUAL 8)
			set(CPACK_SYSTEM_NAME "win64")
		else()
			set(CPACK_SYSTEM_NAME "win32")
		endif()
	elseif(APPLE)
		set(CPACK_SYSTEM_NAME "MacOS")
	else()
		set(CPACK_SYSTEM_NAME "${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR}")
	endif()
endif()
message("CPACK_SYSTEM_NAME: " ${CPACK_SYSTEM_NAME})

set(CPACK_PACKAGE_CONTACT "The Usual Suspects")
set(CPACK_PACKAGE_VENDOR "The Usual Suspects")
set(CPACK_PACKAGE_NAME "TheUsualSuspects")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "The Usual Suspects emulated audio plugin")

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "The Usual Suspects")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://dsp56300.wordpress.com")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

set(CPACK_RPM_PACKAGE_AUTOREQ "yes")
set(CPACK_RPM_PACKAGE_URL ${CPACK_DEBIAN_PACKAGE_HOMEPAGE})
set(CPACK_RPM_PACKAGE_DESCRIPTION ${CPACK_PACKAGE_DESCRIPTION_SUMMARY})

# ----------------- source

add_subdirectory(source)
add_subdirectory(doc)

# ----------------- CPack parameters based on source

get_cmake_property(CPACK_COMPONENTS_ALL COMPONENTS)
list(REMOVE_ITEM CPACK_COMPONENTS_ALL "Unspecified")

# set better archive names
foreach(comp IN LISTS CPACK_COMPONENTS_ALL)
	string(TOUPPER "${comp}" COMP_UPPER)

	set(archiveName "${CPACK_PACKAGE_NAME}-${comp}-${CMAKE_PROJECT_VERSION}-${CPACK_SYSTEM_NAME}")

	set(varName "CPACK_ARCHIVE_${COMP_UPPER}_FILE_NAME")
	set(${varName} ${archiveName})

	set(varName "CPACK_DEBIAN_${COMP_UPPER}_FILE_NAME")
	set(${varName} "${archiveName}.deb")

	set(varName "CPACK_RPM_${COMP_UPPER}_FILE_NAME")
	set(${varName} "${archiveName}.rpm")
endforeach()

# add list of all targets that we want to upload
get_property(CPACK_TUS_TARGETS GLOBAL PROPERTY "TUS_TARGETS")

# add target properties that we need for CPack / upload
foreach(target IN LISTS CPACK_TUS_TARGETS)
	get_target_property(comp "${target}" TUS_CMAKE_COMPONENT)
	if(comp)
		set(CPACK_TUS_${target}_CMAKE_COMPONENT ${comp})
	endif()
	get_target_property(prodName "${target}" TUS_PRODUCT_NAME)
	if(prodName)
		set(CPACK_TUS_${target}_PRODUCT_NAME ${prodName})
	endif()
endforeach()

set(CPACK_TUS_SOURCE_DIR ${CMAKE_SOURCE_DIR})

set(CPACK_COMPONENTS_GROUPING IGNORE)

set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_RPM_COMPONENT_INSTALL ON)

if(UNIX AND NOT APPLE)
	set(CPACK_PACKAGING_INSTALL_PREFIX /usr/local)
endif()

include(CPack)

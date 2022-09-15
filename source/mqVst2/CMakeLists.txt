cmake_minimum_required(VERSION 3.10)

project(mqVst2 VERSION ${CMAKE_PROJECT_VERSION})

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.h.in ${CMAKE_CURRENT_SOURCE_DIR}/version.h)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/versionRC.h.in ${CMAKE_CURRENT_SOURCE_DIR}/versionRC.h)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist)

add_library(mqVst2 STATIC)

target_sources(mqVst2 PRIVATE 
	version.h version.h.in
	mqParameters.cpp mqParameters.h
	vstAudioEffect.cpp vstAudioEffect.h
)

if(WIN32)
	target_sources(mqVst2 PRIVATE version.rc)
endif()

target_include_directories(mqVst2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../vstsdk2.4.2/)
target_link_libraries(mqVst2 vstsdk2 mqLib)

# ____________________________________________________
# DLL
#

add_library(mqVst2dll MODULE)

target_sources(mqVst2dll PRIVATE ../vstsdk2.4.2/public.sdk/source/vst2.x/vstplugmain.cpp)
if(MSVC)
	target_sources(mqVst2dll PRIVATE ../vstsdk2.4.2/public.sdk/samples/vst2.x/win/vstplug.def version.rc)
elseif(APPLE)
	set_target_properties(mqVst2dll PROPERTIES 
		BUNDLE TRUE
		BUNDLE_EXTENSION vst
		XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
		XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
		CXX_VISIBILITY_PRESET default
		CMAKE_C_VISIBILITY_PRESET default
		XCODE_ATTRIBUTE_INFOPLIST_FILE ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist
		MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist
		XCODE_ATTRIBUTE_GENERATE_PKGINFO_FILE "YES"
		XCODE_ATTRIBUTE_WRAPPER_EXTENSION "vst"
		XCODE_ATTRIBUTE_GENERATE_MASTER_OBJECT_FILE "YES"
		XCODE_ATTRIBUTE_OTHER_LDFLAGS "-all_load"
		)
endif()

target_include_directories(mqVst2dll PRIVATE ../vstsdk2.4.2/)

target_link_libraries(mqVst2dll mqVst2)

if(MSVC)
	set_target_properties(mqVst2dll PROPERTIES OUTPUT_NAME "mqEmuVST2${CMAKE_VS_PLATFORM_NAME}")
else()
	set_target_properties(mqVst2dll PROPERTIES OUTPUT_NAME "mqVST2")
endif()

install(TARGETS mqVst2dll DESTINATION . COMPONENT VST2)

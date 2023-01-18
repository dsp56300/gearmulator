include(${CMAKE_CURRENT_LIST_DIR}/../scripts/rclone.cmake)

set(VST2SDK_TESTFILE ${CMAKE_CURRENT_LIST_DIR}/vstsdk2.4.2/public.sdk/source/vst2.x/audioeffect.h)
set(VST2SDK_FOLDER ${CMAKE_CURRENT_LIST_DIR}/vstsdk2.4.2/)

if(NOT EXISTS ${VST2SDK_TESTFILE})
	if(EXISTS ${RCLONE_CONF})
		copyDataFrom("vstsdk2.4.2/" "${VST2SDK_FOLDER}")
	else()
		message(WARNING "rclone.conf not found, unable to copy VST2 SDK")
	endif()
endif()

if(EXISTS ${VST2SDK_TESTFILE})
	set(JUCE_GLOBAL_VST2_SDK_PATH ${VST2SDK_FOLDER})
endif()

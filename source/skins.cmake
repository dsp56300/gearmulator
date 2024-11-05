set(SkinsHeaderDir ${CMAKE_CURRENT_LIST_DIR})

macro(addSkin productName skinName skinFolder skinJson assetName)
	include(${skinFolder}/assets.cmake)

	if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${skinFolder}/${skinJson})
		message(FATAL_ERROR "${CMAKE_CURRENT_SOURCE_DIR}/${skinFolder}/${skinJson} not found")
	endif()

	list(APPEND ASSETS_SKINS ${${assetName}})

	string(JOIN ", " SKIN_CPP_ENTRIES ${SKIN_CPP_ENTRIES} "{\"${skinName}\", \"${skinJson}\", \"\"}")
endmacro()

macro(buildSkinHeader)
	configure_file(${SkinsHeaderDir}/skins.h.in ${CMAKE_CURRENT_SOURCE_DIR}/skins.h)
endmacro()

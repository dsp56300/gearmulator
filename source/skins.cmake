set(SkinsHeaderDir ${CMAKE_CURRENT_LIST_DIR})

macro(addSkin productName skinName skinFolder skinRootFile)
	file(GLOB SKIN_FILES
		RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
		"${skinFolder}/*.json"	# we keep these for now, can be removed once all skins are converted to RML
		"${skinFolder}/*.png"
		"${skinFolder}/*.rml"
		"${skinFolder}/*.rcss"
		"${skinFolder}/*.ttf"
	)
	
	message(STATUS "Skin files for ${skinFolder}: ${SKIN_FILES}")
	message(STATUS "Path test: " ${CMAKE_CURRENT_SOURCE_DIR})

	if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${skinFolder}/${skinRootFile})
		message(FATAL_ERROR "${CMAKE_CURRENT_SOURCE_DIR}/${skinFolder}/${skinRootFile} not found")
	endif()

	list(APPEND ASSETS_SKINS ${SKIN_FILES})

	set(SKIN_FILENAMES "")
	foreach(f ${SKIN_FILES})
	    get_filename_component(fname "${f}" NAME)
		set(fname "\"${fname}\"")
		if(SKIN_FILENAMES STREQUAL "")
		    set(SKIN_FILENAMES ${fname})
		else()
			set(SKIN_FILENAMES "${SKIN_FILENAMES}, ${fname}")
		endif()
	endforeach()

	string(JOIN ", " SKIN_CPP_ENTRIES ${SKIN_CPP_ENTRIES} "{\"${skinName}\", \"${skinRootFile}\", \"\", {${SKIN_FILENAMES}}}")
endmacro()

macro(buildSkinHeader)
	configure_file(${SkinsHeaderDir}/skins.h.in ${CMAKE_CURRENT_SOURCE_DIR}/skins.h)
endmacro()

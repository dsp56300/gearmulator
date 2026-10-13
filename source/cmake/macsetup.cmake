set(MACSETUP_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR})

macro(createMacSetupScript productName)
	set(MAC_SETUP_PRODUCT_NAME ${productName})
	set(macSetupFile ${CMAKE_CURRENT_BINARY_DIR}/macsetup_${productName}.command)
	configure_file(${MACSETUP_SOURCE_DIR}/macsetup.command.in ${macSetupFile})
endmacro()

macro(installMacSetupScript destination component)
	install(FILES ${macSetupFile} DESTINATION ${destination} PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE COMPONENT ${component})
endmacro()

# Installs a console program into its own package component, with the macOS quarantine removal script. The component
# name is the download link on the website (product=<component>), keep it verbatim. EXTRA_DIR installs the contents of
# folders next to the program
function(tus_installConsole target)
	cmake_parse_arguments(arg "" "COMPONENT" "EXTRA_DIR" ${ARGN})
	install(TARGETS ${target} DESTINATION . COMPONENT ${arg_COMPONENT})
	foreach(dir ${arg_EXTRA_DIR})
		install(DIRECTORY ${dir} DESTINATION . COMPONENT ${arg_COMPONENT})
	endforeach()
	if(APPLE)
		createMacSetupScript(${target})
		installMacSetupScript(. ${arg_COMPONENT})
	endif()
endfunction()

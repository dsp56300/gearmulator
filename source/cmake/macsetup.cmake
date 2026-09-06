set(MACSETUP_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR})

macro(createMacSetupScript productName)
	set(MAC_SETUP_PRODUCT_NAME ${productName})
	set(macSetupFile ${CMAKE_CURRENT_BINARY_DIR}/macsetup_${productName}.command)
	configure_file(${MACSETUP_SOURCE_DIR}/macsetup.command.in ${macSetupFile})
endmacro()

macro(installMacSetupScript destination component)
	install(FILES ${macSetupFile} DESTINATION ${destination} PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE COMPONENT ${component})
endmacro()

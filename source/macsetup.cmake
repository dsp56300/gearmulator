set(MACSETUP_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR})

macro(createMacSetupScript productName)
	set(MAC_SETUP_PRODUCT_NAME ${productName})
	set(macSetupFile ${CMAKE_CURRENT_BINARY_DIR}/macsetup.command)
	configure_file(${MACSETUP_SOURCE_DIR}/macsetup.command.in ${macSetupFile})
endmacro()

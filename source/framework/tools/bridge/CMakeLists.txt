add_subdirectory(bridgeLib EXCLUDE_FROM_ALL)
add_subdirectory(client)

# The plugins link the client for remote hardware, only the server is optional
if(${CMAKE_PROJECT_NAME}_COMPONENT_DSPBRIDGE)
	add_subdirectory(server)
endif()

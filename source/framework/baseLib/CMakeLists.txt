cmake_minimum_required(VERSION 3.10)
project(baseLib)

add_library(baseLib STATIC)

set(SOURCES
	binarystream.cpp binarystream.h
	commandline.cpp commandline.h
	compilerdefs.h
	configFile.cpp configFile.h
	endian.h
	event.cpp event.h
	filesystem.cpp filesystem.h
	hybridcontainer.h
	logging.cpp logging.h
	lzo1x.cpp lzo1x.h
	md5.cpp md5.h
	os.cpp os.h
	propertyMap.cpp propertyMap.h
	semaphore.h
	sha1.cpp sha1.h
	sha256.cpp sha256.h
)

target_sources(baseLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

if(NOT MSVC)
	target_link_libraries(baseLib PUBLIC dl)
endif()

set_property(TARGET baseLib PROPERTY FOLDER "Gearmulator")

target_include_directories(baseLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(BUILD_TESTING)
	add_executable(lzo1x_test lzo1x_test.cpp)
	target_link_libraries(lzo1x_test PRIVATE baseLib)
	set_property(TARGET lzo1x_test PROPERTY FOLDER "Tests")
	add_test(NAME lzo1x COMMAND lzo1x_test)
endif()

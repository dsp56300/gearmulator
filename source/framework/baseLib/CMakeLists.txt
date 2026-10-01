cmake_minimum_required(VERSION 3.10)
project(baseLib)

add_library(baseLib STATIC)

set(SOURCES
	adler32.cpp adler32.h
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
	rc4.cpp rc4.h
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
	add_executable(adler32_test adler32_test.cpp)
	target_link_libraries(adler32_test PRIVATE baseLib)
	set_property(TARGET adler32_test PROPERTY FOLDER "Tests")
	add_test(NAME adler32 COMMAND adler32_test)

	add_executable(lzo1x_test lzo1x_test.cpp)
	target_link_libraries(lzo1x_test PRIVATE baseLib)
	set_property(TARGET lzo1x_test PROPERTY FOLDER "Tests")
	add_test(NAME lzo1x COMMAND lzo1x_test)

	add_executable(rc4_test rc4_test.cpp)
	target_link_libraries(rc4_test PRIVATE baseLib)
	set_property(TARGET rc4_test PROPERTY FOLDER "Tests")
	add_test(NAME rc4 COMMAND rc4_test)

	add_executable(hybridcontainer_test hybridcontainer_test.cpp)
	target_link_libraries(hybridcontainer_test PRIVATE baseLib)
	set_property(TARGET hybridcontainer_test PROPERTY FOLDER "Tests")
	add_test(NAME hybridContainer COMMAND hybridcontainer_test)
endif()

cmake_minimum_required(VERSION 3.10)

project(virusLibTest)

add_executable(virusLibTest
	FixtureLoader.cpp
	FixtureLoader.h
	TIControlStateDecoderTest.cpp
	VirusPatchFileParserTest.cpp
	../virusJucePlugin/VirusPatchFileParser.cpp
)
target_link_libraries(virusLibTest PRIVATE virusLib)
target_compile_definitions(virusLibTest PRIVATE VIRUSLIB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata")

add_test(NAME virusLibTests COMMAND virusLibTest)
set_tests_properties(virusLibTests PROPERTIES LABELS "UnitTest")

set_property(TARGET virusLibTest PROPERTY FOLDER "Virus")

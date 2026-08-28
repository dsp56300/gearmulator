cmake_minimum_required(VERSION 3.10)

project(virusLibTest)

add_executable(virusLibTest TIControlStateDecoderTest.cpp)
target_link_libraries(virusLibTest PRIVATE virusLibImport)

add_test(NAME virusLibTests COMMAND virusLibTest)
set_tests_properties(virusLibTests PROPERTIES LABELS "UnitTest")

set_property(TARGET virusLibTest PROPERTY FOLDER "Virus")

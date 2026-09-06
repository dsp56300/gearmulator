cmake_minimum_required(VERSION 3.10)

project(virusRomHashTest)

add_executable(virusRomHashTest)

set(SOURCES romHashTest.cpp)

target_sources(virusRomHashTest PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

target_link_libraries(virusRomHashTest PUBLIC virusLib)

add_test(NAME virusRomHashTests COMMAND virusRomHashTest)
set_tests_properties(virusRomHashTests PROPERTIES LABELS "UnitTest;Virus")

set_property(TARGET virusRomHashTest PROPERTY FOLDER "Virus")

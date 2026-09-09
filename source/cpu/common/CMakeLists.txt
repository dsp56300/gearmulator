cmake_minimum_required(VERSION 3.10)

project(emucommon)

set(SOURCES device.hpp iomux.hpp sched.hpp types.hpp)

add_library(emucommon INTERFACE)
target_include_directories(emucommon INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_compile_features(emucommon INTERFACE cxx_std_20)

add_custom_target(emucommonHeaders SOURCES ${SOURCES})
source_group("source" FILES ${SOURCES})
set_property(TARGET emucommonHeaders PROPERTY FOLDER "cpu")

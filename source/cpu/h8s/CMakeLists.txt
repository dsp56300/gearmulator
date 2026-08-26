cmake_minimum_required(VERSION 3.10)

project(h8s)

# Hitachi/Renesas H8S core. Header only, so this is an interface target that
# exists to hand out the include root. Shared by the Roland devices and the
# Nova/Supernova II devices, which is why it lives under cpu/ rather than
# under a single manufacturer.

set(SOURCES
	h8s.hpp
	h8sdevices.hpp
)

add_library(h8s INTERFACE)

target_include_directories(h8s INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)

# interface targets carry no sources of their own, this is only so the headers
# show up in IDE project trees
add_custom_target(h8sHeaders SOURCES ${SOURCES})
source_group("source" FILES ${SOURCES})

set_property(TARGET h8sHeaders PROPERTY FOLDER "cpu")

cmake_minimum_required(VERSION 3.10)

project(shcore)

# What the SH cores (sh2, sh3) share: the family's instruction set and
# decoder, the cell layout, the instruction effects and the disassembler.
# Header-only; each core adds its own execution model on top.
set(SOURCES cell.hpp decode.hpp disasm.hpp insn.hpp op.hpp)

add_library(shcore INTERFACE)
target_link_libraries(shcore INTERFACE emucommon)
target_compile_features(shcore INTERFACE cxx_std_20)

add_custom_target(shcoreHeaders SOURCES ${SOURCES})
source_group("source" FILES ${SOURCES})
set_property(TARGET shcoreHeaders PROPERTY FOLDER "cpu")

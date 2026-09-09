add_library(nmmLib STATIC nmmpatch.cpp nmmpatch.h nmmrom.cpp nmmrom.h nmmhardware.cpp nmmhardware.h nmmaudioworker.h)
target_link_libraries(nmmLib PUBLIC hardwareLib dsp56kEmu)
target_include_directories(nmmLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_property(TARGET nmmLib PROPERTY FOLDER "NordMicroModular")

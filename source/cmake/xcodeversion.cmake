# XCODE_VERSION is set by CMake when using the Xcode generator, otherwise we need
# to detect it manually here.
if(NOT XCODE_VERSION)
  execute_process(
    COMMAND xcodebuild -version
    OUTPUT_VARIABLE xcodebuild_version
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_FILE /dev/null
  )
  string(REGEX MATCH "Xcode ([0-9][0-9]?([.][0-9])+)" version_match ${xcodebuild_version})
  if(version_match)
    message(STATUS "Identified Xcode Version: ${CMAKE_MATCH_1}")
    set(XCODE_VERSION ${CMAKE_MATCH_1})
  else()
    # If detecting Xcode version failed, set a crazy high version so we default
    # to the newest.
    set(XCODE_VERSION 99)
    message(WARNING "Failed to detect the version of an installed copy of Xcode, falling back to highest supported version. Set XCODE_VERSION to override.")
  endif()
endif()

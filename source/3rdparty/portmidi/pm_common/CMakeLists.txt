# pm_common

# set the build directory for libportmidi.a to be in portmidi, not in 
#    portmidi/pm_common
if(APPLE OR WIN32)
else(APPLE OR WIN32)
  set(LINUX_FLAGS "-DPMALSA")
endif(APPLE OR WIN32)

if(APPLE)
  set(CMAKE_C_FLAGS "-mmacosx-version-min=10.5" CACHE 
      STRING "needed in conjunction with CMAKE_OSX_SYSROOT" FORCE)
endif(APPLE)

macro(prepend_path RESULT PATH)
  set(${RESULT})
  foreach(FILE ${ARGN})
    list(APPEND ${RESULT} "${PATH}${FILE}")
  endforeach(FILE)
endmacro(prepend_path)

# first include the appropriate system-dependent file:
if(UNIX)
  if(APPLE)
    set(MACSRC pmmacosxcm.c pmmac.c readbinaryplist.c finddefault.c)
    prepend_path(LIBSRC ../pm_mac/ ${MACSRC})
    list(APPEND LIBSRC ../porttime/ptmacosx_mach.c)

    include_directories(${CMAKE_OSX_SYSROOT}/Developer/Headers/FlatCarbon)
    set(FRAMEWORK_PATH ${CMAKE_OSX_SYSROOT}/System/Library/Frameworks)
    set(COREAUDIO_LIB "${FRAMEWORK_PATH}/CoreAudio.framework")
    set(COREFOUNDATION_LIB "${FRAMEWORK_PATH}/CoreFoundation.framework")
    set(COREMIDI_LIB "${FRAMEWORK_PATH}/CoreMIDI.framework")
    set(CORESERVICES_LIB "${FRAMEWORK_PATH}/CoreServices.framework")
    set(PM_NEEDED_LIBS ${COREAUDIO_LIB} ${COREFOUNDATION_LIB}
                             ${COREMIDI_LIB} ${CORESERVICES_LIB}
        CACHE INTERNAL "")

    if(PMJAVA)
        set(JAVAVM_LIB "${FRAMEWORK_PATH}/JavaVM.framework")
        set(JAVA_INCLUDE_PATHS ${JAVAVM_LIB}/Headers)
    endif()
    message(STATUS "SYSROOT: " ${CMAKE_OSX_SYSROOT})
  else(APPLE)
    # LINUX settings...
    if(PMJAVA)
        include(FindJNI)
        message(STATUS "JAVA_JVM_LIB_PATH is " ${JAVA_JVM_LIB_PATH})
        message(STATUS "JAVA_INCLUDE_PATH is " ${JAVA_INCLUDE_PATH})
        message(STATUS "JAVA_INCLUDE_PATH2 is " ${JAVA_INCLUDE_PATH2})
        message(STATUS "JAVA_JVM_LIBRARY is " ${JAVA_JVM_LIBRARY})
        set(JAVA_INCLUDE_PATHS ${JAVA_INCLUDE_PATH} ${JAVA_INCLUDE_PATH2})
        # libjvm.so is found relative to JAVA_INCLUDE_PATH:
        set(JAVAVM_LIB ${JAVA_JVM_LIBRARY}/libjvm.so)
    endif()
    set(LINUXSRC pmlinuxalsa.c pmlinux.c finddefault.c)
    prepend_path(LIBSRC ../pm_linux/ ${LINUXSRC})
    list(APPEND LIBSRC ../porttime/ptlinux.c)

    if(NOT ANDROID)
      set(PM_NEEDED_LIBS pthread asound)
	endif()
  endif(APPLE)
else(UNIX)
  if(WIN32)
    # /MDd is multithread debug DLL, /MTd is multithread debug
    # /MD is multithread DLL, /MT is multithread. Change to static:
    include(../pm_win/static.cmake)
    
    if(PMJAVA)
        include(FindJNI)

        set(JAVA_INCLUDE_PATHS ${JAVA_INCLUDE_PATH} ${JAVA_INCLUDE_PATH2})
        # message(STATUS "JAVA_INCLUDE_PATHS: " ${JAVA_INCLUDE_PATHS})
    endif()

    set(WINSRC pmwin.c pmwinmm.c)
    prepend_path(LIBSRC ../pm_win/ ${WINSRC})
    list(APPEND LIBSRC ../porttime/ptwinmm.c)
    set(PM_NEEDED_LIBS winmm.lib)
  endif(WIN32)
endif(UNIX)
if(PMJAVA)
    set(JNI_EXTRA_LIBS ${PM_NEEDED_LIBS} ${JAVA_JVM_LIBRARY})
endif()

# this completes the list of library sources by adding shared code
list(APPEND LIBSRC pmutil.c portmidi.c)

# now add the shared files to make the complete list of library sources
add_library(portmidi-static ${LIBSRC})
set_target_properties(portmidi-static PROPERTIES OUTPUT_NAME "portmidi_s")
target_link_libraries(portmidi-static ${PM_NEEDED_LIBS})

# define the jni library
if(PMJAVA)
    include_directories(${JAVA_INCLUDE_PATHS})

    set(JNISRC ${LIBSRC} ../pm_java/pmjni/pmjni.c)
    add_library(pmjni SHARED ${JNISRC})
    target_link_libraries(pmjni ${JNI_EXTRA_LIBS})
    set_target_properties(pmjni PROPERTIES EXECUTABLE_EXTENSION "jnilib")
endif()

# install the libraries (Linux and Mac OS X command line)
if(UNIX AND PMJAVA)
  INSTALL(TARGETS portmidi-static pmjni
    LIBRARY DESTINATION /usr/local/lib
    ARCHIVE DESTINATION /usr/local/lib)
# .h files installed by pm_dylib/CMakeLists.txt, so don't need them here
#  INSTALL(FILES portmidi.h ../porttime/porttime.h
#    DESTINATION /usr/local/include)
endif(UNIX AND PMJAVA)

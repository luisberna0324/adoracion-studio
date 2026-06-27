# Install script for directory: /Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-src

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/soundtouch" TYPE FILE FILES
    "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-src/include/BPMDetect.h"
    "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-src/include/FIFOSampleBuffer.h"
    "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-src/include/FIFOSamplePipe.h"
    "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-src/include/STTypes.h"
    "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-src/include/SoundTouch.h"
    "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-src/include/soundtouch_config.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-build/libSoundTouch.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libSoundTouch.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libSoundTouch.a")
    execute_process(COMMAND "/usr/bin/ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libSoundTouch.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE EXECUTABLE FILES "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-build/soundstretch")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/soundstretch" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/soundstretch")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" -u -r "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/soundstretch")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  include("/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-build/CMakeFiles/soundstretch.dir/install-cxx-module-bmi-noconfig.cmake" OPTIONAL)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig" TYPE FILE FILES "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-build/soundtouch.pc")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SoundTouch/SoundTouchTargets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SoundTouch/SoundTouchTargets.cmake"
         "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-build/CMakeFiles/Export/fa99ba0aecbeab0836538d30f1ae2cb3/SoundTouchTargets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SoundTouch/SoundTouchTargets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SoundTouch/SoundTouchTargets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/SoundTouch" TYPE FILE FILES "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-build/CMakeFiles/Export/fa99ba0aecbeab0836538d30f1ae2cb3/SoundTouchTargets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^()$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/SoundTouch" TYPE FILE FILES "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-build/CMakeFiles/Export/fa99ba0aecbeab0836538d30f1ae2cb3/SoundTouchTargets-noconfig.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/SoundTouch" TYPE FILE FILES
    "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-build/SoundTouchConfig.cmake"
    "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-build/SoundTouchConfigVersion.cmake"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-build/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()

# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-src")
  file(MAKE_DIRECTORY "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-src")
endif()
file(MAKE_DIRECTORY
  "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-build"
  "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-subbuild/soundtouch-populate-prefix"
  "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-subbuild/soundtouch-populate-prefix/tmp"
  "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-subbuild/soundtouch-populate-prefix/src/soundtouch-populate-stamp"
  "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-subbuild/soundtouch-populate-prefix/src"
  "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-subbuild/soundtouch-populate-prefix/src/soundtouch-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-subbuild/soundtouch-populate-prefix/src/soundtouch-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/_deps/soundtouch-subbuild/soundtouch-populate-prefix/src/soundtouch-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()

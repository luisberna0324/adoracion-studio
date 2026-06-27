# CMake generated Testfile for 
# Source directory: /Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin
# Build directory: /Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[audio_engine_core]=] "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/MTControlsAudioEngineCoreTests")
set_tests_properties([=[audio_engine_core]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/CMakeLists.txt;186;add_test;/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/CMakeLists.txt;0;")
add_test([=[pitch_shift_ring_buffer]=] "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/MTControlsPitchShiftTests")
set_tests_properties([=[pitch_shift_ring_buffer]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/CMakeLists.txt;200;add_test;/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/CMakeLists.txt;0;")
add_test([=[bungee_processor]=] "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build-tests/MTControlsBungeeTests")
set_tests_properties([=[bungee_processor]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/CMakeLists.txt;211;add_test;/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/CMakeLists.txt;0;")
subdirs("JUCE")
subdirs("_deps/soundtouch-build")

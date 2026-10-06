cmake_minimum_required(VERSION 3.22)
project(PlayWorshipBinaryResourceTests LANGUAGES CXX)
enable_testing()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/PlayWorshipAssets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/PlayWorshipBinaryData.cmake")
playworship_add_binary_data(TestBinaryData ${MTCONTROLS_ASSET_FILES})
add_executable(binary_resources binary_resources.cpp)
target_compile_features(binary_resources PRIVATE cxx_std_17)
target_link_libraries(binary_resources PRIVATE TestBinaryData)
add_test(NAME binary_resources COMMAND binary_resources ${MTCONTROLS_ASSET_FILES})
add_test(NAME binary_resources_regeneration
    COMMAND "${CMAKE_COMMAND}"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/regeneration"
        "-DMODULE=${CMAKE_CURRENT_LIST_DIR}/../../cmake/PlayWorshipBinaryData.cmake"
        "-DGENERATOR=${CMAKE_GENERATOR}"
        "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
        "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DEXE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
        -P "${CMAKE_CURRENT_LIST_DIR}/regeneration.cmake")

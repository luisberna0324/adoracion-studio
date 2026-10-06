# Exercise the actual build graph using small, isolated fixture assets.
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/source")
set(source "${WORK_DIR}/source")
set(build "${WORK_DIR}/build")
file(WRITE "${source}/google-g.png" "first")
file(WRITE "${source}/assets.cmake" "set(assets \"${source}/google-g.png\")\n")
file(WRITE "${source}/CMakeLists.txt" "cmake_minimum_required(VERSION 3.22)\nproject(Regeneration LANGUAGES CXX)\ninclude(\"${MODULE}\")\ninclude(assets.cmake)\nplayworship_add_binary_data(FixtureData \${assets})\nadd_executable(check main.cpp)\ntarget_link_libraries(check PRIVATE FixtureData)\n")
file(WRITE "${source}/main.cpp" [=[
#include <BinaryData.h>
#include <iostream>
int main() {
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i) {
        int size = 0;
        const auto* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size);
        if (data == nullptr) return 1;
        std::cout.write(data, size);
        std::cout << '|';
    }
}
]=])

function(run)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Command failed: ${ARGV}\n${output}\n${error}")
    endif()
endfunction()

function(build_and_check expected)
    run("${CMAKE_COMMAND}" --build "${build}" --parallel 2)
    execute_process(COMMAND "${build}/check${EXE_SUFFIX}" RESULT_VARIABLE result OUTPUT_VARIABLE output)
    if(NOT result EQUAL 0 OR NOT output STREQUAL expected)
        message(FATAL_ERROR "Expected '${expected}', received '${output}' (${result})")
    endif()
endfunction()

run("${CMAKE_COMMAND}" -S "${source}" -B "${build}" -G "${GENERATOR}"
    "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}" "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
build_and_check("first|")
file(TIMESTAMP "${build}/FixtureData/BinaryData.h" header_before "%s")
file(TIMESTAMP "${build}/FixtureData/googleg_png.cpp" resource_before "%s")
run("${CMAKE_COMMAND}" -E sleep 1)
run("${CMAKE_COMMAND}" -S "${source}" -B "${build}")
build_and_check("first|")
file(TIMESTAMP "${build}/FixtureData/BinaryData.h" header_after "%s")
file(TIMESTAMP "${build}/FixtureData/googleg_png.cpp" resource_after "%s")
if(NOT header_before STREQUAL header_after OR NOT resource_before STREQUAL resource_after)
    message(FATAL_ERROR "No-op reconfiguration rewrote generated resources")
endif()

# Same-sized replacements must regenerate bytes even though the header is stable.
file(WRITE "${source}/google-g.png" "other")
build_and_check("other|")
run("${CMAKE_COMMAND}" -E sleep 1)
file(WRITE "${source}/google-g.png" "a longer resource")
build_and_check("a longer resource|")

# Adding/removing manifest entries must update sources, counts and lookup tables.
file(WRITE "${source}/demo.pwproj" "demo")
file(WRITE "${source}/assets.cmake" "set(assets \"${source}/google-g.png\" \"${source}/demo.pwproj\")\n")
build_and_check("a longer resource|demo|")
file(WRITE "${source}/assets.cmake" "set(assets \"${source}/demo.pwproj\")\n")
build_and_check("demo|")
message(STATUS "Verified no-op rebuild, same-size and resized assets, manifest additions and removals")

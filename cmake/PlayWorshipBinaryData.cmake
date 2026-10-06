# JUCE-compatible binary resources without a host juceaide build. Used by the
# manual Android target and the SDK-independent resource regression tests.
function(playworship_add_binary_data target)
    set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/${target}")
    file(MAKE_DIRECTORY "${output_dir}")
    set(generator "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/WriteBinaryResource.cmake")
    set(header "#pragma once\nnamespace BinaryData {\n")
    set(table "#include \"BinaryData.h\"\n#include <cstring>\nnamespace BinaryData {\n")
    set(names "")
    set(filenames "")
    set(lookups "")
    set(sources "")
    set(symbols "")

    foreach(asset IN LISTS ARGN)
        get_filename_component(asset "${asset}" ABSOLUTE)
        if(NOT EXISTS "${asset}")
            message(FATAL_ERROR "Missing bundled resource: ${asset}")
        endif()
        get_filename_component(filename "${asset}" NAME)
        # Match JUCE's conversion for our asset filenames, including
        # google-g.png -> googleg_png (the hyphen is removed, not replaced).
        string(REGEX REPLACE "[ .]" "_" symbol "${filename}")
        string(REGEX REPLACE "[^A-Za-z0-9_]" "" symbol "${symbol}")
        if(NOT symbol MATCHES "^[A-Za-z_][A-Za-z0-9_]*$" OR symbol IN_LIST symbols)
            message(FATAL_ERROR "Unsupported or duplicate resource identifier: ${filename}")
        endif()
        list(APPEND symbols "${symbol}")
        file(SIZE "${asset}" size)
        # Reconfigure size constants/table when source bytes change, then only
        # regenerate/recompile affected resource translation units.
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${asset}")
        string(APPEND header "extern const char* ${symbol};\nconst int ${symbol}Size = ${size};\n")
        string(APPEND names "    \"${symbol}\",\n")
        string(APPEND filenames "    \"${filename}\",\n")
        string(APPEND lookups "    if (std::strcmp(name, \"${symbol}\") == 0) { size = ${symbol}Size; return ${symbol}; }\n")
        set(source "${output_dir}/${symbol}.cpp")
        # Also invalidate Makefile builds if a manifest switches to an older
        # file with the same basename, size and resulting resource identifier.
        file(CONFIGURE OUTPUT "${source}.input" CONTENT "${asset}\n" @ONLY)
        add_custom_command(OUTPUT "${source}"
            COMMAND "${CMAKE_COMMAND}" "-DINPUT=${asset}" "-DOUTPUT=${source}" "-DSYMBOL=${symbol}" -P "${generator}"
            DEPENDS "${asset}" "${generator}" "${source}.input"
            COMMENT "Embedding ${filename}"
            VERBATIM)
        list(APPEND sources "${source}")
    endforeach()

    list(LENGTH symbols count)
    string(APPEND header "const int namedResourceListSize = ${count};\nextern const char* namedResourceList[];\nextern const char* originalFilenames[];\nconst char* getNamedResource(const char* name, int& size);\nconst char* getNamedResourceOriginalFilename(const char* name);\n}\n")
    string(APPEND table "const char* namedResourceList[] = {\n${names}};\nconst char* originalFilenames[] = {\n${filenames}};\n")
    string(APPEND table "const char* getNamedResource(const char* name, int& size) {\n    size = 0;\n    if (name == nullptr) return nullptr;\n${lookups}    return nullptr;\n}\n")
    string(APPEND table "const char* getNamedResourceOriginalFilename(const char* name) {\n    if (name != nullptr)\n        for (int i = 0; i < namedResourceListSize; ++i)\n            if (std::strcmp(name, namedResourceList[i]) == 0) return originalFilenames[i];\n    return nullptr;\n}\n}\n")
    # file(CONFIGURE) preserves timestamps when content has not changed.
    file(CONFIGURE OUTPUT "${output_dir}/BinaryData.h" CONTENT "${header}" @ONLY)
    file(CONFIGURE OUTPUT "${output_dir}/BinaryData.cpp" CONTENT "${table}" @ONLY)
    add_library(${target} STATIC "${output_dir}/BinaryData.cpp" ${sources})
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_include_directories(${target} PUBLIC "${output_dir}")
endfunction()

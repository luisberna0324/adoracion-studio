# Source-only offline analysis is independent of playback and cloud services.
include("${CMAKE_CURRENT_LIST_DIR}/WhisperGuide.cmake")
target_sources(MTControls PRIVATE
    src/analysis/SectionDetection.cpp
    src/analysis/GuideSectionAlignment.cpp
    src/analysis/GuideAcousticTemplateMatcher.cpp
    src/analysis/GuideSectionInterpretation.cpp
    src/analysis/GuideModelProvisioner.cpp
    src/analysis/SectionDetectionService.cpp
    src/ui/panels/SectionDetectionPanel.cpp
    src/platform/AppleMusicStructureBridge.cpp)

option(MTCONTROLS_ENABLE_APPLE_MUSIC_STRUCTURE
    "Compile the availability-guarded macOS Swift Music Understanding bridge" ON)
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND MTCONTROLS_ENABLE_APPLE_MUSIC_STRUCTURE)
    # Compile the C-ABI bridge separately rather than enabling Swift on JUCE's
    # targets. A Swift-language dependency changes their archive/link driver and
    # incorrectly passes JUCE's Clang optimization/warning flags to swiftc.
    # Per-architecture archives also support the canonical universal Ninja build.
    execute_process(COMMAND xcrun --find swiftc RESULT_VARIABLE _section_swift_result
        OUTPUT_VARIABLE _section_swiftc OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _section_swift_result EQUAL 0 OR NOT EXISTS "${_section_swiftc}")
        message(STATUS "Section detection: Swift compiler unavailable; manual section editor retained")
    else()
        if(CMAKE_OSX_SYSROOT)
            set(_section_sdk_selection "${CMAKE_OSX_SYSROOT}")
        else()
            set(_section_sdk_selection macosx)
        endif()
        execute_process(COMMAND xcrun --sdk "${_section_sdk_selection}" --show-sdk-path
            RESULT_VARIABLE _section_sdk_result
            OUTPUT_VARIABLE _section_sdk OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT _section_sdk_result EQUAL 0)
            message(FATAL_ERROR "Cannot resolve the selected macOS SDK for the section detection bridge")
        endif()
        if(CMAKE_OSX_ARCHITECTURES)
            set(_section_architectures ${CMAKE_OSX_ARCHITECTURES})
        else()
            set(_section_architectures "${CMAKE_HOST_SYSTEM_PROCESSOR}")
        endif()
        if(CMAKE_OSX_DEPLOYMENT_TARGET)
            set(_section_min_os "${CMAKE_OSX_DEPLOYMENT_TARGET}")
        else()
            set(_section_min_os "12.0")
        endif()
        set(_section_bridge_root "${CMAKE_CURRENT_BINARY_DIR}/section-detection/$<CONFIG>")
        set(_section_bridge_source "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/AppleMusicStructure.swift")
        set(_section_archives)
        foreach(_section_arch IN LISTS _section_architectures)
            if(NOT _section_arch MATCHES "^(arm64|x86_64)$")
                message(FATAL_ERROR "Unsupported macOS section detection architecture: ${_section_arch}")
            endif()
            set(_section_archive "${_section_bridge_root}/${_section_arch}/libPWMusicStructure.a")
            add_custom_command(OUTPUT "${_section_archive}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${_section_bridge_root}/${_section_arch}"
                COMMAND "${_section_swiftc}" -parse-as-library -swift-version 5
                    "$<IF:$<CONFIG:Debug>,-Onone,-O>"
                    -target "${_section_arch}-apple-macosx${_section_min_os}" -sdk "${_section_sdk}"
                    -emit-library -static -module-name PWMusicStructure
                    -o "${_section_archive}" "${_section_bridge_source}"
                DEPENDS "${_section_bridge_source}"
                COMMENT "Building offline section analysis bridge (${_section_arch}, macOS ${_section_min_os})"
                VERBATIM)
            list(APPEND _section_archives "${_section_archive}")
        endforeach()
        set(_section_universal_archive "${_section_bridge_root}/libPWMusicStructure.a")
        list(LENGTH _section_archives _section_arch_count)
        if(_section_arch_count GREATER 1)
            add_custom_command(OUTPUT "${_section_universal_archive}"
                COMMAND xcrun lipo -create ${_section_archives} -output "${_section_universal_archive}"
                DEPENDS ${_section_archives} VERBATIM)
        else()
            add_custom_command(OUTPUT "${_section_universal_archive}"
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${_section_archives}" "${_section_universal_archive}"
                DEPENDS ${_section_archives} VERBATIM)
        endif()
        add_custom_target(PWMusicStructureBuild DEPENDS "${_section_universal_archive}")
        add_library(PWMusicStructure STATIC IMPORTED GLOBAL)
        set_target_properties(PWMusicStructure PROPERTIES IMPORTED_LOCATION "${_section_universal_archive}")
        # Imported locations do not expand CONFIG expressions; specify actual
        # paths for multi-configuration generators as well as single-config ones.
        if(CMAKE_CONFIGURATION_TYPES)
            foreach(_section_config IN LISTS CMAKE_CONFIGURATION_TYPES)
                string(TOUPPER "${_section_config}" _section_config_upper)
                set_property(TARGET PWMusicStructure PROPERTY "IMPORTED_LOCATION_${_section_config_upper}"
                    "${CMAKE_CURRENT_BINARY_DIR}/section-detection/${_section_config}/libPWMusicStructure.a")
            endforeach()
        else()
            set_property(TARGET PWMusicStructure PROPERTY IMPORTED_LOCATION
                "${CMAKE_CURRENT_BINARY_DIR}/section-detection/${CMAKE_BUILD_TYPE}/libPWMusicStructure.a")
        endif()
        add_dependencies(PWMusicStructure PWMusicStructureBuild)
        get_filename_component(_section_swift_bin "${_section_swiftc}" DIRECTORY)
        get_filename_component(_section_swift_usr "${_section_swift_bin}" DIRECTORY)
        # Mach-O autolinks system Swift libraries. Back-deployment compatibility
        # archives live in the toolchain; a link search path suffices and avoids
        # embedding an absolute Xcode path in the app's runtime search paths.
        target_link_options(PWMusicStructure INTERFACE
            "-L${_section_swift_usr}/lib/swift/macosx")
        target_link_libraries(PWMusicStructure INTERFACE
            "-framework Foundation" "-framework AVFoundation" "-framework CoreMedia")
        if(EXISTS "${_section_sdk}/System/Library/Frameworks/MusicUnderstanding.framework")
            target_link_libraries(PWMusicStructure INTERFACE "-weak_framework MusicUnderstanding")
            message(STATUS "Section detection: MusicUnderstanding SDK found; native branch requires macOS27 runtime validation")
        else()
            message(STATUS "Section detection: SDK has no MusicUnderstanding; compiling guarded Swift unavailable branch")
        endif()
        target_compile_definitions(MTControls PRIVATE PLAYWORSHIP_APPLE_MUSIC_STRUCTURE_BRIDGE=1)
        target_link_libraries(MTControls PRIVATE PWMusicStructure)
    endif()
endif()

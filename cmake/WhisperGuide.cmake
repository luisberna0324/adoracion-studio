# Local-only guide recognition. The model is provisioned separately and never
# downloaded by the application. Disable this option for a dependency-free
# portable fallback (in particular, mobile builds are opt-in until validated).
include_guard(GLOBAL)
set(_pw_whisper_default ON)
if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(_pw_whisper_default OFF)
endif()
option(MTCONTROLS_ENABLE_WHISPER_GUIDE "Build the local Whisper guide transcriber" ${_pw_whisper_default})
set(MTCONTROLS_WHISPER_SOURCE_DIR "" CACHE PATH "Offline whisper.cpp v1.9.5 source checkout override")
option(MTCONTROLS_BUILD_GUIDE_TRANSCRIPTION_PROBE "Build the opt-in real-model guide transcription probe" ${MTCONTROLS_BUILD_TESTS})

function(_playworship_add_whisper)
    # Function scope prevents third-party option defaults from changing the
    # shared/static mode or output directories of JUCE and the app.
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(BUILD_SHARED_LIBS OFF)
    set(WHISPER_BUILD_IS_DEV OFF)
    set(WHISPER_BUILD_TESTS OFF)
    set(WHISPER_BUILD_EXAMPLES OFF)
    set(WHISPER_BUILD_SERVER OFF)
    set(WHISPER_USE_SYSTEM_GGML OFF)
    set(WHISPER_USE_SYSTEM_LLAMA OFF)
    set(WHISPER_CURL OFF)
    set(WHISPER_COREML OFF)
    set(WHISPER_OPENVINO OFF)
    set(GGML_BACKEND_DL OFF)
    # GGML_STATIC adds a system-wide -static link flag; BUILD_SHARED_LIBS=OFF
    # already produces the static archives needed by the application.
    set(GGML_STATIC OFF)
    set(GGML_NATIVE OFF)
    set(GGML_CPU ON)
    set(GGML_CPU_ALL_VARIANTS OFF)
    set(GGML_CPU_KLEIDIAI OFF)
    set(GGML_ACCELERATE ${APPLE})
    foreach(_backend IN ITEMS METAL BLAS CUDA MUSA HIP VULKAN WEBGPU SYCL RPC
                              OPENCL OPENVINO HEXAGON ZDNN ZENDNN ET VIRTGPU
                              VIRTGPU_BACKEND OPENMP LLAMAFILE)
        set(GGML_${_backend} OFF)
    endforeach()
    set(GGML_METAL_EMBED_LIBRARY OFF)
    set(GGML_BUILD_TESTS OFF)
    set(GGML_BUILD_EXAMPLES OFF)
    set(GGML_CCACHE OFF)
    if(MTCONTROLS_WHISPER_SOURCE_DIR)
        if(NOT EXISTS "${MTCONTROLS_WHISPER_SOURCE_DIR}/include/whisper.h")
            message(FATAL_ERROR "MTCONTROLS_WHISPER_SOURCE_DIR must contain whisper.cpp v1.9.5")
        endif()
        file(READ "${MTCONTROLS_WHISPER_SOURCE_DIR}/CMakeLists.txt" _whisper_cmake)
        foreach(_version_part IN ITEMS "MAJOR 1" "MINOR 9" "PATCH 5")
            if(NOT _whisper_cmake MATCHES "WHISPER_VERSION_${_version_part}\\)")
                message(FATAL_ERROR "The offline Whisper source override must be v1.9.5")
            endif()
        endforeach()
        add_subdirectory("${MTCONTROLS_WHISPER_SOURCE_DIR}"
            "${CMAKE_BINARY_DIR}/playworship-whisper" EXCLUDE_FROM_ALL)
        set(_whisper_source "${MTCONTROLS_WHISPER_SOURCE_DIR}")
    else()
        include(FetchContent)
        FetchContent_Declare(playworship_whisper
            URL https://codeload.github.com/ggml-org/whisper.cpp/tar.gz/refs/tags/v1.9.5
            URL_HASH SHA256=ff1a9053feb509ff9d7729703355541ae9690073a6b1c40eb692c962e0dc1720
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            EXCLUDE_FROM_ALL)
        FetchContent_MakeAvailable(playworship_whisper)
        set(_whisper_source "${playworship_whisper_SOURCE_DIR}")
    endif()
    set_target_properties(whisper ggml ggml-base ggml-cpu PROPERTIES POSITION_INDEPENDENT_CODE ON)
    # Reuse the application's existing ThirdPartyLicenses distribution location.
    # The upstream root license covers whisper.cpp and its bundled ggml sources.
    file(READ "${_whisper_source}/LICENSE" _whisper_license_text)
    set(_whisper_license "${CMAKE_BINARY_DIR}/licenses/Whisper-ggml-MIT.txt")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/licenses")
    file(WRITE "${_whisper_license}"
        "whisper.cpp and bundled ggml, v1.9.5\nhttps://github.com/ggml-org/whisper.cpp/tree/v1.9.5\n\n${_whisper_license_text}")
    if(TARGET MTControls_Standalone)
        if(APPLE)
            set_source_files_properties("${_whisper_license}" PROPERTIES
                MACOSX_PACKAGE_LOCATION "Resources/ThirdPartyLicenses")
            target_sources(MTControls_Standalone PRIVATE "${_whisper_license}")
        else()
            add_custom_command(TARGET MTControls_Standalone POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E make_directory
                    "$<TARGET_FILE_DIR:MTControls_Standalone>/ThirdPartyLicenses"
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_whisper_license}"
                    "$<TARGET_FILE_DIR:MTControls_Standalone>/ThirdPartyLicenses/Whisper-ggml-MIT.txt"
                VERBATIM)
        endif()
    endif()
endfunction()

if(MTCONTROLS_ENABLE_WHISPER_GUIDE)
    _playworship_add_whisper()
    message(STATUS "Guide transcription: whisper.cpp v1.9.5, static CPU/Accelerate; no Metal or network")
else()
    message(STATUS "Guide transcription: portable unavailable provider (Whisper disabled)")
endif()

function(playworship_link_whisper_guide target)
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/analysis/WhisperGuideTranscriber.cpp")
    if(MTCONTROLS_ENABLE_WHISPER_GUIDE)
        target_compile_definitions(${target} PRIVATE PLAYWORSHIP_WHISPER_GUIDE=1)
        target_link_libraries(${target} PRIVATE whisper)
    endif()
endfunction()

playworship_link_whisper_guide(MTControls)
if(MTCONTROLS_BUILD_GUIDE_TRANSCRIPTION_PROBE)
    juce_add_console_app(MTControlsGuideTranscriptionProbe PRODUCT_NAME "Guide Transcription Probe")
    target_sources(MTControlsGuideTranscriptionProbe PRIVATE tests/guide_transcription_probe.cpp)
    target_include_directories(MTControlsGuideTranscriptionProbe PRIVATE src)
    target_compile_definitions(MTControlsGuideTranscriptionProbe PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries(MTControlsGuideTranscriptionProbe PRIVATE
        juce::juce_audio_formats juce::juce_cryptography)
    juce_generate_juce_header(MTControlsGuideTranscriptionProbe)
    playworship_link_whisper_guide(MTControlsGuideTranscriptionProbe)
    if(MSVC)
        target_compile_options(MTControlsGuideTranscriptionProbe PRIVATE /utf-8)
    endif()
    # No CTest entry: normal regression runs do not require/download a model or
    # private audio. Invoke this probe explicitly for native recognition proof.
endif()

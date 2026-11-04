include_guard(GLOBAL)
if(NOT CMAKE_SCRIPT_MODE_FILE)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/../src/AppVersion.h")
endif()
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../src/AppVersion.h" _version_line
    REGEX "^#define PLAYWORSHIP_RELEASE_VERSION ")
if(NOT _version_line MATCHES "^#define PLAYWORSHIP_RELEASE_VERSION \"([0-9]+\\.[0-9]+\\.[0-9]+)\"$")
    message(FATAL_ERROR "Invalid canonical version in src/AppVersion.h")
endif()
set(PLAYWORSHIP_RELEASE_VERSION "${CMAKE_MATCH_1}")
foreach(_requested "${MTCONTROLS_APP_VERSION}" "$ENV{MTCONTROLS_APP_VERSION}")
    if(NOT _requested STREQUAL "" AND NOT _requested STREQUAL PLAYWORSHIP_RELEASE_VERSION)
        message(FATAL_ERROR
            "Requested version ${_requested} differs from source ${PLAYWORSHIP_RELEASE_VERSION}. Use a fresh build directory or update -DMTCONTROLS_APP_VERSION.")
    endif()
endforeach()
set(MTCONTROLS_APP_VERSION "${PLAYWORSHIP_RELEASE_VERSION}" CACHE STRING
    "Canonical PlayWorship release version" FORCE)

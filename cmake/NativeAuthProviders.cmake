# Authentication providers follow the target OS, never the build host.
# Keep the legacy cache key for diagnostics, but normalize stale/manual values
# so existing Apple build directories cannot silently retain the old OFF flag.
function(mtcontrols_configure_native_auth_providers)
    set(apple_sign_in OFF)
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(apple_sign_in ON)
    endif()
    set(MTCONTROLS_APPLE_SIGN_IN_ENABLED "${apple_sign_in}" CACHE BOOL
        "Native Apple sign-in: enabled on iOS/macOS only (derived from target platform)" FORCE)
    set(MTCONTROLS_APPLE_SIGN_IN_ENABLED "${apple_sign_in}" PARENT_SCOPE)
endfunction()

#!/bin/sh

set -eu

app_path=${1:-build/ios-ipad-flatbundle/MTControls_artefacts/Release/Standalone/PlayWorship.app}
signing_mode=${2:-distribution}

fail() {
    echo "FAIL: $1" >&2
    exit 1
}

pass() {
    echo "PASS: $1"
}

[ -d "$app_path" ] || fail "App bundle not found: $app_path"
[ -f "$app_path/Info.plist" ] || fail "Info.plist is missing"
[ -f "$app_path/PrivacyInfo.xcprivacy" ] || fail "PrivacyInfo.xcprivacy is missing from the app bundle"

plutil -lint "$app_path/Info.plist" >/dev/null || fail "Info.plist is invalid"
plutil -lint "$app_path/PrivacyInfo.xcprivacy" >/dev/null || fail "PrivacyInfo.xcprivacy is invalid"
pass "property lists are valid"

bundle_id=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$app_path/Info.plist")
[ "$bundle_id" = "com.multitracks.playworship" ] || fail "Unexpected bundle identifier: $bundle_id"
pass "bundle identifier is $bundle_id"

version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app_path/Info.plist")
build_number=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$app_path/Info.plist")
echo "$version" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' || fail "Invalid marketing version: $version"
echo "$build_number" | grep -Eq '^[1-9][0-9]*$' || fail "Build number must be a positive integer: $build_number"
pass "version $version ($build_number)"

full_screen=$(/usr/libexec/PlistBuddy -c 'Print :UIRequiresFullScreen' "$app_path/Info.plist")
[ "$full_screen" = "true" ] || fail "UIRequiresFullScreen must be true for a landscape-only universal app"

orientations=$(/usr/libexec/PlistBuddy -c 'Print :UISupportedInterfaceOrientations' "$app_path/Info.plist")
echo "$orientations" | grep -q 'UIInterfaceOrientationLandscapeLeft' || fail "Landscape Left is missing"
echo "$orientations" | grep -q 'UIInterfaceOrientationLandscapeRight' || fail "Landscape Right is missing"
echo "$orientations" | grep -q 'Portrait' && fail "Portrait orientation must not be present"
pass "landscape-only full-screen configuration"

encryption=$(/usr/libexec/PlistBuddy -c 'Print :ITSAppUsesNonExemptEncryption' "$app_path/Info.plist")
[ "$encryption" = "false" ] || fail "ITSAppUsesNonExemptEncryption must be false for exempt standard encryption"
pass "export-compliance declaration is present"

icon_count=0
for icon_path in "$app_path"/AppIcon*.png; do
    [ -f "$icon_path" ] || continue
    icon_count=$((icon_count + 1))
    alpha=$(sips -g hasAlpha "$icon_path" 2>/dev/null | awk '/hasAlpha:/ { print $2 }')
    [ "$alpha" = "no" ] || fail "App icon contains alpha: $icon_path"
done
[ "$icon_count" -gt 0 ] || fail "No compiled app icons were found"
pass "compiled app icons have no alpha channel"

codesign --verify --deep --strict --verbose=2 "$app_path" >/dev/null 2>&1 || fail "Code signature is invalid"
pass "code signature is valid"

debug_entitlement=$(
    codesign -d --entitlements :- "$app_path" 2>/dev/null |
        plutil -extract get-task-allow raw -o - - 2>/dev/null || true
)
if [ "$debug_entitlement" = "true" ]; then
    [ "$signing_mode" = "--allow-development-signing" ] ||
        fail "Submission build contains get-task-allow=true; export the archive for App Store Connect"
    pass "development-signed archive is valid and still requires App Store export"
else
    pass "distribution entitlements do not allow debugging"
fi

sdk_name=$(/usr/libexec/PlistBuddy -c 'Print :DTSDKName' "$app_path/Info.plist")
echo "$sdk_name" | grep -Eq '^iphoneos(2[6-9]|[3-9][0-9])' || fail "Build must use the iOS 26 SDK or newer: $sdk_name"
pass "built with $sdk_name"

echo "App Store binary preflight passed for $app_path"

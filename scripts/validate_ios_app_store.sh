#!/bin/sh

set -eu

app_path=${1:-build/ios-appstore/archive/PlayWorship.xcarchive/Products/Applications/PlayWorship.app}
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

if /usr/libexec/PlistBuddy -c 'Print :CFBundleDocumentTypes' "$app_path/Info.plist" >/dev/null 2>&1; then
    documents_in_place=$(/usr/libexec/PlistBuddy -c 'Print :LSSupportsOpeningDocumentsInPlace' "$app_path/Info.plist" 2>/dev/null || true)
    [ "$documents_in_place" = "false" ] || fail "Document imports must explicitly declare LSSupportsOpeningDocumentsInPlace=false"
    document_browser=$(/usr/libexec/PlistBuddy -c 'Print :UISupportsDocumentBrowser' "$app_path/Info.plist" 2>/dev/null || true)
    [ "$document_browser" != "true" ] || fail "The app does not implement UIDocumentBrowserViewController"
    pass "document types explicitly use copy-based import, not in-place editing"
fi

full_screen=$(/usr/libexec/PlistBuddy -c 'Print :UIRequiresFullScreen' "$app_path/Info.plist")
[ "$full_screen" = "false" ] || fail "UIRequiresFullScreen must be false to allow scene resizing on iPad"

device_families=$(/usr/libexec/PlistBuddy -c 'Print :UIDeviceFamily' "$app_path/Info.plist")
echo "$device_families" | grep -Eq '^[[:space:]]*1[[:space:]]*$' || fail "iPhone device family is missing"
echo "$device_families" | grep -Eq '^[[:space:]]*2[[:space:]]*$' || fail "iPad device family is missing"

orientations=$(/usr/libexec/PlistBuddy -c 'Print :UISupportedInterfaceOrientations' "$app_path/Info.plist")
echo "$orientations" | grep -q 'UIInterfaceOrientationLandscapeLeft' || fail "Landscape Left is missing"
echo "$orientations" | grep -q 'UIInterfaceOrientationLandscapeRight' || fail "Landscape Right is missing"
echo "$orientations" | grep -q 'UIInterfaceOrientationPortrait' || fail "iPhone portrait is missing for Library and Practice"
ipad_orientations=$(/usr/libexec/PlistBuddy -c 'Print :UISupportedInterfaceOrientations~ipad' "$app_path/Info.plist")
echo "$ipad_orientations" | grep -q 'UIInterfaceOrientationLandscapeLeft' || fail "iPad Landscape Left is missing"
echo "$ipad_orientations" | grep -q 'UIInterfaceOrientationLandscapeRight' || fail "iPad Landscape Right is missing"
echo "$ipad_orientations" | grep -Eq '^[[:space:]]*UIInterfaceOrientationPortrait[[:space:]]*$' || fail "iPad Portrait is missing"
echo "$ipad_orientations" | grep -Eq '^[[:space:]]*UIInterfaceOrientationPortraitUpsideDown[[:space:]]*$' || fail "iPad Portrait Upside Down is missing"
pass "universal device families, iPhone portrait/landscape and all four iPad orientations"

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

case "$app_path" in
    *.xcarchive/Products/Applications/*.app)
        executable=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$app_path/Info.plist")
        app_bundle_name=$(basename "$app_path")
        app_dsym="$app_path/../../../dSYMs/$app_bundle_name.dSYM"
        [ -d "$app_dsym" ] || fail "Archive is missing the app dSYM: $app_dsym"
        binary_uuids=$(xcrun dwarfdump --uuid "$app_path/$executable" | awk '/^UUID:/ {print $2, $3}' | sort)
        symbol_uuids=$(xcrun dwarfdump --uuid "$app_dsym" | awk '/^UUID:/ {print $2, $3}' | sort)
        [ -n "$binary_uuids" ] || fail "App binary UUID cannot be read"
        [ "$binary_uuids" = "$symbol_uuids" ] || fail "Archive app dSYM UUID does not match the executable"
        pass "archive app dSYM matches the executable UUID"
        ;;
esac

[ -f "$app_path/embedded.mobileprovision" ] || fail "Provisioning profile is missing"
profile_plist=$(mktemp)
trap 'rm -f "$profile_plist"' EXIT HUP INT TERM
security cms -D -i "$app_path/embedded.mobileprovision" -o "$profile_plist" >/dev/null 2>&1 || fail "Provisioning profile cannot be decoded"
profile_team=$(/usr/libexec/PlistBuddy -c 'Print :TeamIdentifier:0' "$profile_plist")
signed_app_identifier=$(
    codesign -d --entitlements :- "$app_path" 2>/dev/null |
        plutil -extract application-identifier raw -o - - 2>/dev/null || true
)
[ "$signed_app_identifier" = "$profile_team.$bundle_id" ] || fail "Signed application identifier does not match the provisioning team and bundle ID"
pass "provisioning profile matches the signed app"

debug_entitlement=$(
    codesign -d --entitlements :- "$app_path" 2>/dev/null |
        plutil -extract get-task-allow raw -o - - 2>/dev/null || true
)
if [ "$debug_entitlement" = "true" ]; then
    [ "$signing_mode" = "--allow-development-signing" ] ||
        fail "Submission build contains get-task-allow=true; export the archive for App Store Connect"
    pass "development-signed archive is valid and still requires App Store export"
else
    if /usr/libexec/PlistBuddy -c 'Print :ProvisionedDevices' "$profile_plist" >/dev/null 2>&1; then
        fail "Device-limited provisioning is not an App Store distribution profile"
    fi
    provisions_all_devices=$(/usr/libexec/PlistBuddy -c 'Print :ProvisionsAllDevices' "$profile_plist" 2>/dev/null || true)
    [ "$provisions_all_devices" != "true" ] || fail "Enterprise provisioning is not an App Store distribution profile"
    pass "distribution entitlements do not allow debugging"
fi

sdk_name=$(/usr/libexec/PlistBuddy -c 'Print :DTSDKName' "$app_path/Info.plist")
echo "$sdk_name" | grep -Eq '^iphoneos(2[6-9]|[3-9][0-9])' || fail "Build must use the iOS 26 SDK or newer: $sdk_name"
pass "built with $sdk_name"

echo "App Store binary preflight passed for $app_path"

#!/usr/bin/env python3
"""Validate a signed Mac App Store .app without contacting Apple or changing it."""

import argparse
from pathlib import Path
import plistlib
import re
import subprocess
import sys


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def command(*args):
    result = subprocess.run(args, capture_output=True, text=True, check=False)
    require(result.returncode == 0, f"{' '.join(args[:2])} failed: {result.stderr.strip()}")
    return result.stdout, result.stderr


def validate(app, team_id, allow_development):
    contents = app / "Contents"
    require(app.is_dir() and app.suffix == ".app", "Pass an existing macOS .app bundle")
    info = plistlib.loads((contents / "Info.plist").read_bytes())
    require(info.get("CFBundleIdentifier") == "com.multitracks.playworship", "Wrong Bundle ID")
    require(info.get("LSApplicationCategoryType") == "public.app-category.music", "Missing Music category")
    require(re.fullmatch(r"[1-9][0-9]*", str(info.get("CFBundleVersion", ""))), "Invalid Mac build number")
    require(info.get("CFBundleSupportedPlatforms") == ["MacOSX"], "Wrong supported platform")
    require(info.get("ITSAppUsesNonExemptEncryption") is False, "Encryption declaration must be reviewed")
    executable = contents / "MacOS" / info.get("CFBundleExecutable", "")
    require(executable.is_file(), "Main executable missing")

    privacy = plistlib.loads((contents / "Resources" / "PrivacyInfo.xcprivacy").read_bytes())
    require(privacy.get("NSPrivacyTracking") is False, "Unexpected tracking declaration")
    require(privacy.get("NSPrivacyTrackingDomains") == [], "Unexpected tracking domains")
    expected_data = {"EmailAddress", "UserID", "DeviceID", "PurchaseHistory", "AudioData", "OtherUserContent"}
    actual_data = set()
    for entry in privacy.get("NSPrivacyCollectedDataTypes", []):
        actual_data.add(entry.get("NSPrivacyCollectedDataType", "").removeprefix("NSPrivacyCollectedDataType"))
        require(entry.get("NSPrivacyCollectedDataTypeLinked") is True, "Unexpected data linkage")
        require(entry.get("NSPrivacyCollectedDataTypeTracking") is False, "Unexpected data tracking")
        require(entry.get("NSPrivacyCollectedDataTypePurposes") == ["NSPrivacyCollectedDataTypePurposeAppFunctionality"],
                "Unexpected data purpose; reconcile App Privacy")
    require(actual_data == expected_data, "Data inventory changed; reconcile App Privacy")
    reasons = {entry.get("NSPrivacyAccessedAPIType"): set(entry.get("NSPrivacyAccessedAPITypeReasons", []))
               for entry in privacy.get("NSPrivacyAccessedAPITypes", [])}
    require(reasons == {"NSPrivacyAccessedAPICategoryFileTimestamp": {"C617.1", "3B52.1"},
                        "NSPrivacyAccessedAPICategorySystemBootTime": {"35F9.1"},
                        "NSPrivacyAccessedAPICategoryDiskSpace": {"E174.1"}},
            "Required-reason API inventory changed; review it before distribution")

    command("/usr/bin/codesign", "--verify", "--deep", "--strict", str(app))
    _, signature = command("/usr/bin/codesign", "-dv", "--verbose=4", str(app))
    require("Signature=adhoc" not in signature, "Ad-hoc signatures cannot be distributed")
    require(f"TeamIdentifier={team_id}" in signature.splitlines(), "Unexpected signing team")
    authorities = [line.removeprefix("Authority=") for line in signature.splitlines() if line.startswith("Authority=")]
    require(authorities, "Signing certificate chain missing")
    expected_authorities = ("Apple Development:", "Mac Developer:") if allow_development else ()
    expected_authorities += ("Apple Distribution:", "3rd Party Mac Developer Application:")
    require(any(authorities[0].startswith(value) for value in expected_authorities),
            "Not a Mac App Store signing identity (development is allowed only with its explicit flag)")
    xml, _ = command("/usr/bin/codesign", "-d", "--entitlements", ":-", str(app))
    entitlements = plistlib.loads(xml.encode())
    for key in ("com.apple.security.app-sandbox", "com.apple.security.network.client",
                "com.apple.security.network.server", "com.apple.security.device.usb",
                "com.apple.security.device.bluetooth", "com.apple.security.files.user-selected.read-write"):
        require(entitlements.get(key) is True, f"Required entitlement missing: {key}")
    require(not entitlements.get("com.apple.security.get-task-allow") or allow_development,
            "Distribution bundle allows debugging")
    require(not any("temporary-exception" in key or key.endswith("all-files") for key in entitlements),
            "Unexpected broad sandbox exception")

    macho = {b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe",
             b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca", b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca"}
    binaries = set()
    for item in contents.rglob("*"):
        require(not item.is_symlink() or item.resolve().is_relative_to(app.resolve()),
                "Bundle contains a symlink outside the app")
        require("ffmpeg" not in item.name.lower(), "The helper-free Store profile contains FFmpeg")
        if not item.is_file():
            continue
        with item.open("rb") as source:
            if source.read(4) in macho:
                binaries.add(item.resolve())
    require(binaries, "No Mach-O executable found")
    for binary in sorted(binaries):
        relative = binary.relative_to(app.resolve())
        require(binary == executable.resolve() or "Frameworks" in relative.parts,
                f"Unexpected executable helper: {relative}")
        command("/usr/bin/codesign", "--verify", "--strict", str(binary))
        _, nested_signature = command("/usr/bin/codesign", "-dv", "--verbose=4", str(binary))
        require(f"TeamIdentifier={team_id}" in nested_signature.splitlines(), f"Nested code has wrong team: {relative}")
        dependencies, _ = command("/usr/bin/otool", "-L", str(binary))
        for line in dependencies.splitlines():
            if "compatibility version" not in line:
                continue
            dependency = line.strip().split(" (", 1)[0]
            require(dependency.startswith(("/System/Library/", "/usr/lib/", "@rpath/", "@loader_path/", "@executable_path/")),
                    f"Dependency outside the bundle/system: {dependency}")
            if dependency.startswith("@rpath/"):
                require((contents / "Frameworks" / dependency.removeprefix("@rpath/")).exists(),
                        f"Bundled dependency missing: {dependency}")
    print(f"PASS: Mac bundle, privacy, sandbox, signing team and {len(binaries)} Mach-O files verified.")
    print("App Review, backend/privacy practices and on-device sandbox workflows still require verification.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    parser.add_argument("--team-id", default="V9T23PX769")
    parser.add_argument("--allow-development-signing", action="store_true",
                        help="Validate a development-signed local candidate; not a distribution approval")
    args = parser.parse_args()
    try:
        validate(args.app.resolve(), args.team_id, args.allow_development_signing)
    except (OSError, ValueError, RuntimeError, plistlib.InvalidFileException) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

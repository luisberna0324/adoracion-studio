#!/usr/bin/env bash
set -euo pipefail

test -f third_party/JUCE/CMakeLists.txt
bash scripts/bootstrap_bungee.sh
printf 'sdk.dir=%s\n' "${ANDROID_SDK_ROOT}" > android/local.properties

cd android
./gradlew :app:assembleDebug --no-daemon
test -s app/build/outputs/apk/debug/app-debug.apk

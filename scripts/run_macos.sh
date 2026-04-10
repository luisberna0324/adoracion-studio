#!/usr/bin/env bash
set -euo pipefail

export JUCE_DIR="/Users/luisberna/dev/multitracks/multitracks/third_party/JUCE"
export MTCONTROLS_API_BASE_URL="${MTCONTROLS_API_BASE_URL:-https://playworship-345830648064.us-east1.run.app}"
export MTCONTROLS_SESSION_REAUTH_MINUTES="${MTCONTROLS_SESSION_REAUTH_MINUTES:-7200}"
export MTCONTROLS_REAUTH_GRACE_MINUTES="${MTCONTROLS_REAUTH_GRACE_MINUTES:-300}"
export MTCONTROLS_FFMPEG_BIN_MACOS="${MTCONTROLS_FFMPEG_BIN_MACOS:-/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/third_party/ffmpeg/macos/ffmpeg}"

cmake --preset macos-release \
  -DMTCONTROLS_API_BASE_URL="$MTCONTROLS_API_BASE_URL" \
  -DMTCONTROLS_SESSION_REAUTH_MINUTES="$MTCONTROLS_SESSION_REAUTH_MINUTES" \
  -DMTCONTROLS_REAUTH_GRACE_MINUTES="$MTCONTROLS_REAUTH_GRACE_MINUTES" \
  -DMTCONTROLS_FFMPEG_BIN_MACOS="$MTCONTROLS_FFMPEG_BIN_MACOS"
cmake --build --preset macos-release

APP_DIR="/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build/macos-release/MTControls_artefacts/Release/Standalone"
if compgen -G "$APP_DIR/*.app" > /dev/null; then
  APP_PATH="$(find "$APP_DIR" -maxdepth 1 -type d -name "*.app" -print -quit)"
  if open "$APP_PATH"; then
    exit 0
  fi
  APP_EXEC_NAME="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$APP_PATH/Contents/Info.plist" 2>/dev/null || true)"
  if [[ -z "$APP_EXEC_NAME" ]]; then
    APP_EXEC_NAME="$(basename "$APP_PATH" .app)"
  fi
  EXEC_PATH="$APP_PATH/Contents/MacOS/$APP_EXEC_NAME"
  echo "Could not open app bundle in this environment."
  echo "Launch manually with:"
  echo "  \"$EXEC_PATH\""
  exit 0
fi

FALLBACK_DIR="/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build/macos-release/MTControls_artefacts/Standalone"
if compgen -G "$FALLBACK_DIR/*.app" > /dev/null; then
  APP_PATH="$(find "$FALLBACK_DIR" -maxdepth 1 -type d -name "*.app" -print -quit)"
  if open "$APP_PATH"; then
    exit 0
  fi
  APP_EXEC_NAME="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$APP_PATH/Contents/Info.plist" 2>/dev/null || true)"
  if [[ -z "$APP_EXEC_NAME" ]]; then
    APP_EXEC_NAME="$(basename "$APP_PATH" .app)"
  fi
  EXEC_PATH="$APP_PATH/Contents/MacOS/$APP_EXEC_NAME"
  echo "Could not open app bundle in this environment."
  echo "Launch manually with:"
  echo "  \"$EXEC_PATH\""
else
  echo "No .app found in $APP_DIR or $FALLBACK_DIR"
fi

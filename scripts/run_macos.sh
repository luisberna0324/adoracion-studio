#!/usr/bin/env bash
set -euo pipefail

export JUCE_DIR="/Users/luisberna/dev/multitracks/multitracks/third_party/JUCE"
export MTCONTROLS_API_BASE_URL="${MTCONTROLS_API_BASE_URL:-https://playworship-345830648064.us-east1.run.app}"
export MTCONTROLS_SESSION_REAUTH_MINUTES="${MTCONTROLS_SESSION_REAUTH_MINUTES:-7200}"
export MTCONTROLS_REAUTH_GRACE_MINUTES="${MTCONTROLS_REAUTH_GRACE_MINUTES:-300}"

cmake --preset macos-release \
  -DMTCONTROLS_API_BASE_URL="$MTCONTROLS_API_BASE_URL" \
  -DMTCONTROLS_SESSION_REAUTH_MINUTES="$MTCONTROLS_SESSION_REAUTH_MINUTES" \
  -DMTCONTROLS_REAUTH_GRACE_MINUTES="$MTCONTROLS_REAUTH_GRACE_MINUTES"
cmake --build --preset macos-release

APP_DIR="/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build/macos-release/MTControls_artefacts/Release/Standalone"
if compgen -G "$APP_DIR/*.app" > /dev/null; then
  APP_PATH="$(find "$APP_DIR" -maxdepth 1 -type d -name "*.app" -print -quit)"
  if open "$APP_PATH"; then
    exit 0
  fi
  EXEC_PATH="$APP_PATH/Contents/MacOS/Adoracion Estudio"
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
  EXEC_PATH="$APP_PATH/Contents/MacOS/Adoracion Estudio"
  echo "Could not open app bundle in this environment."
  echo "Launch manually with:"
  echo "  \"$EXEC_PATH\""
else
  echo "No .app found in $APP_DIR or $FALLBACK_DIR"
fi

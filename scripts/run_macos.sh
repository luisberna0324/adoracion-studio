#!/usr/bin/env bash
set -euo pipefail

export JUCE_DIR="/Users/luisberna/dev/multitracks/multitracks/third_party/JUCE"
export MTCONTROLS_API_BASE_URL="${MTCONTROLS_API_BASE_URL:-http://localhost:3000}"
export MTCONTROLS_SESSION_REAUTH_MINUTES="${MTCONTROLS_SESSION_REAUTH_MINUTES:-7200}"
export MTCONTROLS_REAUTH_GRACE_MINUTES="${MTCONTROLS_REAUTH_GRACE_MINUTES:-300}"

cmake --preset macos-release \
  -DMTCONTROLS_API_BASE_URL="$MTCONTROLS_API_BASE_URL" \
  -DMTCONTROLS_SESSION_REAUTH_MINUTES="$MTCONTROLS_SESSION_REAUTH_MINUTES" \
  -DMTCONTROLS_REAUTH_GRACE_MINUTES="$MTCONTROLS_REAUTH_GRACE_MINUTES"
cmake --build --preset macos-release

APP_DIR="/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build/macos-release/PlayWorship_artefacts/Release/Standalone"
if compgen -G "$APP_DIR/*.app" > /dev/null; then
  open "$APP_DIR"/*.app
  exit 0
fi

FALLBACK_DIR="/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build/macos-release/PlayWorship_artefacts/Standalone"
if compgen -G "$FALLBACK_DIR/*.app" > /dev/null; then
  open "$FALLBACK_DIR"/*.app
else
  echo "No .app found in $APP_DIR or $FALLBACK_DIR"
fi

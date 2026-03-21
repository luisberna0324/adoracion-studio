#!/usr/bin/env bash
set -euo pipefail

export JUCE_DIR="/Users/luisberna/dev/multitracks/multitracks/third_party/JUCE"

cmake --preset macos-release
cmake --build --preset macos-release

APP_DIR="/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build/macos-release/MTControls_artefacts/Release/Standalone"
if compgen -G "$APP_DIR/*.app" > /dev/null; then
  open "$APP_DIR"/*.app
  exit 0
fi

FALLBACK_DIR="/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build/macos-release/MTControls_artefacts/Standalone"
if compgen -G "$FALLBACK_DIR/*.app" > /dev/null; then
  open "$FALLBACK_DIR"/*.app
else
  echo "No .app found in $APP_DIR or $FALLBACK_DIR"
fi

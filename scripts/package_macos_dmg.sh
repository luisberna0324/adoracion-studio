#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/package_macos_dmg.sh --version <semver> [--standalone-dir <dir>] [--output-dir <dir>]

Defaults:
  --standalone-dir build/macos-release/MTControls_artefacts/Release/Standalone
  --output-dir dist
EOF
}

VERSION=""
STANDALONE_DIR="build/macos-release/MTControls_artefacts/Release/Standalone"
OUTPUT_DIR="dist"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --version)
      VERSION="${2:-}"
      shift 2
      ;;
    --standalone-dir)
      STANDALONE_DIR="${2:-}"
      shift 2
      ;;
    --output-dir)
      OUTPUT_DIR="${2:-}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ -z "${VERSION}" ]]; then
  echo "--version is required" >&2
  usage
  exit 1
fi

if [[ ! -d "${STANDALONE_DIR}" ]]; then
  echo "Standalone directory not found: ${STANDALONE_DIR}" >&2
  exit 1
fi

if ! compgen -G "${STANDALONE_DIR}"/*.app >/dev/null; then
  echo "No .app found in ${STANDALONE_DIR}" >&2
  exit 1
fi

mkdir -p "${OUTPUT_DIR}"

TMP_DIR="$(mktemp -d)"
cleanup() {
  rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

STAGE_DIR="${TMP_DIR}/dmg-root"
mkdir -p "${STAGE_DIR}"

# Keep full standalone output content in the installer image.
cp -R "${STANDALONE_DIR}/." "${STAGE_DIR}/"
ln -s /Applications "${STAGE_DIR}/Applications"

DMG_PATH="${OUTPUT_DIR}/playworship-macos-installer-${VERSION}.dmg"
rm -f "${DMG_PATH}"

hdiutil create \
  -volname "MTControls Installer ${VERSION}" \
  -srcfolder "${STAGE_DIR}" \
  -ov \
  -format UDZO \
  "${DMG_PATH}"

echo "Created ${DMG_PATH}"

#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/package_macos_dmg.sh --version <semver> [--standalone-dir <dir>] [--output-dir <dir>] [--ffmpeg-bin <path>]

Defaults:
  --standalone-dir build/macos-release/MTControls_artefacts/Release/Standalone
  --output-dir dist
  --ffmpeg-bin third_party/ffmpeg/macos/ffmpeg (or MTCONTROLS_FFMPEG_BIN_MACOS env)
EOF
}

VERSION=""
STANDALONE_DIR="build/macos-release/MTControls_artefacts/Release/Standalone"
OUTPUT_DIR="dist"
FFMPEG_BIN="${MTCONTROLS_FFMPEG_BIN_MACOS:-}"

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
    --ffmpeg-bin)
      FFMPEG_BIN="${2:-}"
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

if [[ ! -d "${STANDALONE_DIR}" ]] || ! compgen -G "${STANDALONE_DIR}"/*.app >/dev/null; then
  for candidate in \
    "build/macos-release/MTControls_artefacts/Standalone" \
    "build/macos-release/MTControls_artefacts/Release/Standalone" \
    "build/macos-release/MTControls_artefacts/Debug/Standalone"; do
    if [[ -d "${candidate}" ]] && compgen -G "${candidate}"/*.app >/dev/null; then
      STANDALONE_DIR="${candidate}"
      break
    fi
  done
fi

if [[ ! -d "${STANDALONE_DIR}" ]] || ! compgen -G "${STANDALONE_DIR}"/*.app >/dev/null; then
  echo "No .app found for Standalone packaging." >&2
  echo "Checked: ${STANDALONE_DIR}" >&2
  echo "Hint: build Standalone target first, for example:" >&2
  echo "  cmake --build --preset macos-release --target MTControls_Standalone" >&2
  exit 1
fi

if [[ -z "${FFMPEG_BIN}" ]]; then
  if [[ -f "third_party/ffmpeg/macos/ffmpeg" ]]; then
    FFMPEG_BIN="third_party/ffmpeg/macos/ffmpeg"
  fi
fi

if [[ -z "${FFMPEG_BIN}" || ! -f "${FFMPEG_BIN}" ]]; then
  echo "ffmpeg binary not found. Set --ffmpeg-bin or MTCONTROLS_FFMPEG_BIN_MACOS." >&2
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

# Ensure ffmpeg is embedded inside every app bundle in the DMG payload.
while IFS= read -r -d '' app_path; do
  ffmpeg_dir="${app_path}/Contents/Resources/ffmpeg"
  mkdir -p "${ffmpeg_dir}"
  cp -f "${FFMPEG_BIN}" "${ffmpeg_dir}/ffmpeg"
  chmod +x "${ffmpeg_dir}/ffmpeg"
done < <(find "${STAGE_DIR}" -maxdepth 1 -type d -name "*.app" -print0)

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

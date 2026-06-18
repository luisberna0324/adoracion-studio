#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/package_macos_dmg.sh --version <semver> [--standalone-dir <dir>] [--output-dir <dir>] [--ffmpeg-bin <path>] [--guides-zip <file>] [--clicks-zip <file>]

Defaults:
  --standalone-dir build/macos-release/MTControls_artefacts/Release/Standalone
  --output-dir dist
  --ffmpeg-bin third_party/ffmpeg/macos/ffmpeg (or MTCONTROLS_FFMPEG_BIN_MACOS env, optional)
  --guides-zip assets/guides/guides-en-es.zip (or MTCONTROLS_GUIDES_ZIP_MACOS env, optional)
  --clicks-zip assets/clicks/clicks-pack.zip (or MTCONTROLS_CLICKS_ZIP_MACOS env, optional)
EOF
}

VERSION=""
STANDALONE_DIR="build/macos-release/MTControls_artefacts/Release/Standalone"
OUTPUT_DIR="dist"
FFMPEG_BIN="${MTCONTROLS_FFMPEG_BIN_MACOS:-}"
GUIDES_ZIP="${MTCONTROLS_GUIDES_ZIP_MACOS:-}"
CLICKS_ZIP="${MTCONTROLS_CLICKS_ZIP_MACOS:-}"

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
    --guides-zip)
      GUIDES_ZIP="${2:-}"
      shift 2
      ;;
    --clicks-zip)
      CLICKS_ZIP="${2:-}"
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
if [[ -z "${GUIDES_ZIP}" && -f "assets/guides/guides-en-es.zip" ]]; then
  GUIDES_ZIP="assets/guides/guides-en-es.zip"
fi
if [[ -z "${CLICKS_ZIP}" && -f "assets/clicks/clicks-pack.zip" ]]; then
  CLICKS_ZIP="assets/clicks/clicks-pack.zip"
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

if [[ -n "${FFMPEG_BIN}" && -f "${FFMPEG_BIN}" ]]; then
  # Optional legacy behavior: include ffmpeg inside app bundle when explicitly provided.
  while IFS= read -r -d '' app_path; do
    ffmpeg_dir="${app_path}/Contents/Resources/ffmpeg"
    mkdir -p "${ffmpeg_dir}"
    cp -f "${FFMPEG_BIN}" "${ffmpeg_dir}/ffmpeg"
    chmod +x "${ffmpeg_dir}/ffmpeg"
  done < <(find "${STAGE_DIR}" -maxdepth 1 -type d -name "*.app" -print0)
else
  echo "ffmpeg binary not provided; installer will bootstrap encoder at runtime."
fi

if [[ -n "${GUIDES_ZIP}" ]]; then
  if [[ ! -f "${GUIDES_ZIP}" ]]; then
    echo "Guides zip not found: ${GUIDES_ZIP}" >&2
    exit 1
  fi
  while IFS= read -r -d '' app_path; do
    guides_dir="${app_path}/Contents/Resources/guides"
    mkdir -p "${guides_dir}"
    cp -f "${GUIDES_ZIP}" "${guides_dir}/guides-en-es.zip"
  done < <(find "${STAGE_DIR}" -maxdepth 1 -type d -name "*.app" -print0)
else
  echo "guides-en-es.zip not provided; dynamic guide assets may fail to bootstrap."
fi

if [[ -n "${CLICKS_ZIP}" ]]; then
  if [[ ! -f "${CLICKS_ZIP}" ]]; then
    echo "Clicks zip not found: ${CLICKS_ZIP}" >&2
    exit 1
  fi
  while IFS= read -r -d '' app_path; do
    clicks_dir="${app_path}/Contents/Resources/clicks"
    mkdir -p "${clicks_dir}"
    cp -f "${CLICKS_ZIP}" "${clicks_dir}/clicks-pack.zip"
  done < <(find "${STAGE_DIR}" -maxdepth 1 -type d -name "*.app" -print0)
else
  echo "clicks-pack.zip not provided; metronome click assets may fall back to runtime bootstrap."
fi

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

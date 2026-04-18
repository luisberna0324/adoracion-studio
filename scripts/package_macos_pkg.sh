#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  scripts/package_macos_pkg.sh --version <semver> [--standalone-dir <dir>] [--output-dir <dir>] [--identifier <bundle-id>]

Defaults:
  --standalone-dir build/macos-release/MTControls_artefacts/Release/Standalone
  --output-dir dist
  --identifier com.multitracks.playworship
USAGE
}

VERSION=""
STANDALONE_DIR="build/macos-release/MTControls_artefacts/Release/Standalone"
OUTPUT_DIR="dist"
IDENTIFIER="com.multitracks.playworship"

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
    --identifier)
      IDENTIFIER="${2:-}"
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
  exit 1
fi

POSTINSTALL_SCRIPT="packaging/macos/postinstall"
if [[ ! -f "${POSTINSTALL_SCRIPT}" ]]; then
  echo "Missing postinstall script: ${POSTINSTALL_SCRIPT}" >&2
  exit 1
fi

mkdir -p "${OUTPUT_DIR}"
TMP_DIR="$(mktemp -d)"
cleanup() {
  rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

STAGE_ROOT="${TMP_DIR}/pkg-root"
SCRIPTS_DIR="${TMP_DIR}/pkg-scripts"
mkdir -p "${STAGE_ROOT}" "${SCRIPTS_DIR}"

APP_PATH="$(find "${STANDALONE_DIR}" -maxdepth 1 -type d -name '*.app' -print -quit)"
if [[ -z "${APP_PATH}" ]]; then
  echo "No .app found in ${STANDALONE_DIR}" >&2
  exit 1
fi

cp -R "${APP_PATH}" "${STAGE_ROOT}/"
cp -f "${POSTINSTALL_SCRIPT}" "${SCRIPTS_DIR}/postinstall"
chmod +x "${SCRIPTS_DIR}/postinstall"

PKG_PATH="${OUTPUT_DIR}/playworship-macos-installer-${VERSION}.pkg"
rm -f "${PKG_PATH}"

pkgbuild \
  --root "${STAGE_ROOT}" \
  --identifier "${IDENTIFIER}.installer" \
  --version "${VERSION}" \
  --install-location "/Applications" \
  --scripts "${SCRIPTS_DIR}" \
  "${PKG_PATH}"

echo "Created ${PKG_PATH}"

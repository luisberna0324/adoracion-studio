#!/bin/bash
set -euo pipefail

# Script to package the JUCE application as a .deb installer for Linux (Ubuntu/Debian)

VERSION=""
STANDALONE_DIR=""
OUTPUT_DIR="dist"
FFMPEG_BIN=""

usage() {
    echo "Usage: $0 --version <v> --standalone-dir <dir> [--output-dir <dir>] [--ffmpeg-bin <path>]"
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --version) VERSION="$2"; shift 2 ;;
        --standalone-dir) STANDALONE_DIR="$2"; shift 2 ;;
        --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
        --ffmpeg-bin) FFMPEG_BIN="$2"; shift 2 ;;
        *) usage ;;
    esac
done

[[ -n "${VERSION}" ]] || usage
[[ -n "${STANDALONE_DIR}" ]] || usage

[[ -f "${STANDALONE_DIR}/libbungee.so" ]] || {
    echo "Required Bungee runtime missing: ${STANDALONE_DIR}/libbungee.so" >&2
    exit 1
}
[[ -f "${STANDALONE_DIR}/ThirdPartyLicenses/Bungee-MPL-2.0.txt" ]] || {
    echo "Required Bungee license missing from Standalone output." >&2
    exit 1
}

# Normalize version for debian (no leading v, lowercase)
DEB_VERSION="${VERSION#v}"
PKG_NAME="playworship"
ARCH="amd64"
DEB_FILE="${PKG_NAME}_${DEB_VERSION}_${ARCH}.deb"

echo "Packaging ${PKG_NAME} version ${DEB_VERSION} for Linux..."

# Create staging directory
STAGE_DIR=$(mktemp -d)
trap 'rm -rf "${STAGE_DIR}"' EXIT

mkdir -p "${STAGE_DIR}/usr/bin"
mkdir -p "${STAGE_DIR}/usr/lib/${PKG_NAME}"
mkdir -p "${STAGE_DIR}/usr/share/applications"
mkdir -p "${STAGE_DIR}/usr/share/icons/hicolor/1024x1024/apps"
mkdir -p "${STAGE_DIR}/DEBIAN"

# Copy binary and resources
# We expect STANDALONE_DIR to contain the MTControls binary and ffmpeg folder (if bundled)
cp -r "${STANDALONE_DIR}/"* "${STAGE_DIR}/usr/lib/${PKG_NAME}/"

# Create a executable symlink or wrapper in /usr/bin
cat > "${STAGE_DIR}/usr/bin/${PKG_NAME}" <<EOF
#!/bin/bash
set -e
exec "/usr/lib/${PKG_NAME}/PlayWorship" "\$@"
EOF
chmod +x "${STAGE_DIR}/usr/bin/${PKG_NAME}"

# Copy icon if exists
if [[ -f "assets/icons/app-icon-1024.png" ]]; then
    cp "assets/icons/app-icon-1024.png" "${STAGE_DIR}/usr/share/icons/hicolor/1024x1024/apps/${PKG_NAME}.png"
fi

# Create .desktop file
cat > "${STAGE_DIR}/usr/share/applications/${PKG_NAME}.desktop" <<EOF
[Desktop Entry]
Name=PlayWorship
Comment=Professional worship playback application
Exec=${PKG_NAME}
Icon=${PKG_NAME}
Terminal=false
Type=Application
Categories=AudioVideo;Audio;
EOF

# Create DEBIAN/control file
# Note: dependencies are a rough guess for JUCE apps on modern Ubuntu
cat > "${STAGE_DIR}/DEBIAN/control" <<EOF
Package: ${PKG_NAME}
Version: ${DEB_VERSION}
Section: sound
Priority: optional
Architecture: ${ARCH}
Depends: libasound2, libfreetype6, libfontconfig1, libx11-6, libxext6, libxinerama1, libxrandr2, libxcursor1, libcurl4
Maintainer: Multitracks <support@multitracks.com>
Description: PlayWorship multi-track playback engine.
 Professional worship playback application with stem mixing and metronome support.
EOF

# Build the package
mkdir -p "${OUTPUT_DIR}"
dpkg-deb --build "${STAGE_DIR}" "${OUTPUT_DIR}/${DEB_FILE}"

echo "Linux package created: ${OUTPUT_DIR}/${DEB_FILE}"

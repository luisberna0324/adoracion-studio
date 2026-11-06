#!/usr/bin/env bash
# Build the optional desktop encoder. This script never uploads or installs it.
set -euo pipefail

FFMPEG_VERSION="8.0.1"
FFMPEG_SHA256="05ee0b03119b45c0bdb4df654b96802e909e0a752f72e4fe3794f487229e5a41"
LAME_VERSION="3.100"
LAME_SHA256="ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e"
MACOS_MIN="12.0"
FFMPEG_URL="https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz"
LAME_URL="https://downloads.sourceforge.net/project/lame/lame/${LAME_VERSION}/lame-${LAME_VERSION}.tar.gz"
ENCODER_WORK_DIR="${HOME}/Library/Caches/PlayWorship/encoder-builds/ffmpeg-${FFMPEG_VERSION}-lame-${LAME_VERSION}-x86_64"
ENCODER_OUTPUT_DIR=""
ENCODER_SOURCE_DIR=""
ENCODER_JOBS="$(sysctl -n hw.logicalcpu 2>/dev/null || printf '2')"

usage() {
  cat <<'USAGE'
Usage: scripts/build_macos_audio_encoder.sh [options]

  --work-dir DIR    Source cache and unique build directory (default: user cache).
  --source-dir DIR  Read the two pinned source archives here; do not download.
  --output-dir DIR  New local artifact directory (default: unique build's dist).
  --jobs N          Parallel make jobs (default: logical CPU count).
  -h, --help        Show this help without building or downloading.

Requires macOS, Xcode Command Line Tools, make, curl, tar and shasum.
Produces an Intel x86_64 audio-only FFmpeg for macOS 12.0 or newer, together
with original sources, license texts, build configuration and SHA256SUMS.
No packages are installed, existing outputs replaced, or artifacts published.
USAGE
}

fail() { printf 'Error: %s\n' "$*" >&2; exit 1; }
need_value() { [[ $# -ge 2 && -n "$2" ]] || fail "Missing value for $1"; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --work-dir) need_value "$@"; ENCODER_WORK_DIR="$2"; shift 2 ;;
    --source-dir) need_value "$@"; ENCODER_SOURCE_DIR="$2"; shift 2 ;;
    --output-dir) need_value "$@"; ENCODER_OUTPUT_DIR="$2"; shift 2 ;;
    --jobs) need_value "$@"; ENCODER_JOBS="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) fail "Unknown option: $1" ;;
  esac
done

[[ "$(uname -s)" == "Darwin" ]] || fail "Build this macOS artifact on macOS."
[[ "$ENCODER_JOBS" =~ ^[1-9][0-9]*$ ]] || fail "--jobs must be a positive integer."
# Upstream configure/make files cannot reliably preserve spaces in build flags.
[[ ! "$ENCODER_WORK_DIR" =~ [[:space:]] ]] || fail "--work-dir must not contain whitespace."
for encoder_tool in xcrun make curl tar shasum awk install otool lipo; do
  command -v "$encoder_tool" >/dev/null 2>&1 || fail "Missing tool: $encoder_tool"
done
ENCODER_CC="$(xcrun --find clang)"
ENCODER_SDK="$(xcrun --sdk macosx --show-sdk-path)"
mkdir -p "$ENCODER_WORK_DIR"
ENCODER_WORK_DIR="$(cd "$ENCODER_WORK_DIR" && pwd -P)"
ENCODER_RUN_DIR="$(mktemp -d "${ENCODER_WORK_DIR}/build.XXXXXX")"
ENCODER_PREFIX="${ENCODER_RUN_DIR}/prefix"
ENCODER_DIST="${ENCODER_RUN_DIR}/dist"
mkdir -p "$ENCODER_RUN_DIR/sources" "$ENCODER_RUN_DIR/src" "$ENCODER_PREFIX" "$ENCODER_DIST"
if [[ -z "$ENCODER_OUTPUT_DIR" ]]; then
  ENCODER_OUTPUT_DIR="$ENCODER_DIST"
elif [[ -e "$ENCODER_OUTPUT_DIR" || -L "$ENCODER_OUTPUT_DIR" ]]; then
  fail "Output already exists; choose a new --output-dir: $ENCODER_OUTPUT_DIR"
fi

verify_source() {
  local archive="$1" expected="$2" actual
  actual="$(shasum -a 256 "$archive" | awk '{print $1}')"
  [[ "$actual" == "$expected" ]] || fail "Source checksum mismatch: $archive"
}

prepare_source() {
  local name="$1" url="$2" expected="$3" archive
  archive="${ENCODER_RUN_DIR}/sources/${name}"
  if [[ -n "$ENCODER_SOURCE_DIR" ]]; then
    [[ -f "${ENCODER_SOURCE_DIR}/${name}" ]] || fail "Missing pinned source: ${ENCODER_SOURCE_DIR}/${name}"
    cp "${ENCODER_SOURCE_DIR}/${name}" "$archive"
  else
    mkdir -p "$ENCODER_WORK_DIR/downloads"
    if [[ ! -f "${ENCODER_WORK_DIR}/downloads/${name}" ]]; then
      curl --fail --location --proto '=https' --proto-redir '=https' --tlsv1.2 --retry 3 \
        --output "${archive}.download" "$url"
      verify_source "${archive}.download" "$expected"
      mv "${archive}.download" "${ENCODER_WORK_DIR}/downloads/${name}"
    fi
    cp "${ENCODER_WORK_DIR}/downloads/${name}" "$archive"
  fi
  verify_source "$archive" "$expected"
  tar -xf "$archive" -C "$ENCODER_RUN_DIR/src"
}

prepare_source "ffmpeg-${FFMPEG_VERSION}.tar.xz" "$FFMPEG_URL" "$FFMPEG_SHA256"
prepare_source "lame-${LAME_VERSION}.tar.gz" "$LAME_URL" "$LAME_SHA256"
ENCODER_FFMPEG_SRC="${ENCODER_RUN_DIR}/src/ffmpeg-${FFMPEG_VERSION}"
ENCODER_LAME_SRC="${ENCODER_RUN_DIR}/src/lame-${LAME_VERSION}"
ENCODER_CFLAGS="-arch x86_64 -march=x86-64 -mno-avx -mno-avx2 -mmacosx-version-min=${MACOS_MIN}"
ENCODER_LDFLAGS="-arch x86_64 -mmacosx-version-min=${MACOS_MIN}"

lame_configure=(
  "--prefix=${ENCODER_PREFIX}" --host=x86_64-apple-darwin
  --enable-static --disable-shared --disable-frontend --disable-decoder
  --disable-nasm
)
ffmpeg_configure=(
  "--prefix=${ENCODER_PREFIX}" "--cc=${ENCODER_CC}"
  --arch=x86_64 --target-os=darwin --enable-cross-compile --cpu=generic
  "--sysroot=${ENCODER_SDK}"
  --disable-asm --disable-x86asm --disable-autodetect --disable-network
  --disable-everything --enable-ffmpeg --disable-ffplay --disable-ffprobe
  --disable-doc --disable-debug --disable-avdevice --disable-swscale
  --enable-static --disable-shared --disable-gpl --disable-nonfree
  --enable-libmp3lame
  '--enable-decoder=pcm*,adpcm*,aac,aac_latm,alac,flac,mp3*,vorbis,opus,wma*'
  --enable-encoder=flac,aac,libmp3lame,pcm_s16le,pcm_s24le
  --enable-demuxer=wav,aiff,flac,mp3,mov,aac,ogg,asf
  --enable-muxer=flac,mp3,ipod,mp4,adts,wav
  --enable-parser=aac,aac_latm,flac,mpegaudio,vorbis,opus
  --enable-protocol=file,pipe --enable-filter=aresample,aformat,anull
  "--extra-cflags=${ENCODER_CFLAGS} -I${ENCODER_PREFIX}/include"
  "--extra-ldflags=${ENCODER_LDFLAGS} -L${ENCODER_PREFIX}/lib"
)

printf 'Building Intel audio encoder in %s\n' "$ENCODER_RUN_DIR"
(
  # Do not inherit flags or libraries targeting the build machine's CPU.
  export CC="$ENCODER_CC" CFLAGS="$ENCODER_CFLAGS" LDFLAGS="$ENCODER_LDFLAGS"
  export CPPFLAGS="" CXXFLAGS="" SDKROOT="$ENCODER_SDK" MACOSX_DEPLOYMENT_TARGET="$MACOS_MIN"
  export PKG_CONFIG_PATH="" PKG_CONFIG_LIBDIR="${ENCODER_PREFIX}/lib/pkgconfig"
  export ZERO_AR_DATE=1
  cd "$ENCODER_LAME_SRC"
  ./configure "${lame_configure[@]}" >"${ENCODER_RUN_DIR}/lame-configure.log" 2>&1
  make -j "$ENCODER_JOBS" >"${ENCODER_RUN_DIR}/lame-make.log" 2>&1
  make install >"${ENCODER_RUN_DIR}/lame-install.log" 2>&1
  cd "$ENCODER_FFMPEG_SRC"
  ./configure "${ffmpeg_configure[@]}" >"${ENCODER_RUN_DIR}/ffmpeg-configure.log" 2>&1
  make -j "$ENCODER_JOBS" ffmpeg >"${ENCODER_RUN_DIR}/ffmpeg-make.log" 2>&1
)

# Inspect the binary without executing a cross-compiled artifact.
[[ "$(lipo -archs "$ENCODER_FFMPEG_SRC/ffmpeg")" == "x86_64" ]] || fail "Unexpected Mach-O architecture."
otool -L "$ENCODER_FFMPEG_SRC/ffmpeg" >"${ENCODER_RUN_DIR}/dependencies.txt"
awk '
  NR > 1 && $1 !~ /^\/usr\/lib\// && $1 !~ /^\/System\/Library\// { bad = 1 }
  END { exit bad }
' "${ENCODER_RUN_DIR}/dependencies.txt" || fail "Encoder links a non-system dynamic library."
[[ -f "$ENCODER_FFMPEG_SRC/config.h" ]] || fail "Missing FFmpeg configuration."
awk '
  $1 == "#define" && $2 ~ /^CONFIG_(GPL|NONFREE|GPLV3)$/ && $3 == 1 { bad = 1 }
  END { exit bad }
' "$ENCODER_FFMPEG_SRC/config.h" || fail "Unexpected GPL/nonfree license option."

install -m 755 "$ENCODER_FFMPEG_SRC/ffmpeg" "$ENCODER_DIST/ffmpeg"
mkdir -p "$ENCODER_DIST/sources" "$ENCODER_DIST/licenses" "$ENCODER_DIST/build-config"
cp "$ENCODER_RUN_DIR/sources/ffmpeg-${FFMPEG_VERSION}.tar.xz" "$ENCODER_DIST/sources/"
cp "$ENCODER_RUN_DIR/sources/lame-${LAME_VERSION}.tar.gz" "$ENCODER_DIST/sources/"
cp "$ENCODER_FFMPEG_SRC/COPYING.LGPLv2.1" "$ENCODER_DIST/licenses/FFmpeg-COPYING.LGPLv2.1"
cp "$ENCODER_FFMPEG_SRC/LICENSE.md" "$ENCODER_DIST/licenses/FFmpeg-LICENSE.md"
cp "$ENCODER_LAME_SRC/COPYING" "$ENCODER_DIST/licenses/LAME-COPYING"
cp "$ENCODER_LAME_SRC/README" "$ENCODER_DIST/licenses/LAME-README"
cp "$ENCODER_FFMPEG_SRC/config.h" "$ENCODER_DIST/build-config/ffmpeg-config.h"
cp "$ENCODER_FFMPEG_SRC/ffbuild/config.mak" "$ENCODER_DIST/build-config/ffmpeg-config.mak"
cp "$ENCODER_LAME_SRC/config.h" "$ENCODER_DIST/build-config/lame-config.h"
cp "$ENCODER_RUN_DIR/dependencies.txt" "$ENCODER_DIST/build-config/"
otool -l "$ENCODER_DIST/ffmpeg" >"$ENCODER_DIST/build-config/macho-load-commands.txt"
cp "$0" "$ENCODER_DIST/build-config/build_macos_audio_encoder.sh"
{
  printf 'PlayWorship optional macOS audio encoder\n\n'
  printf 'FFmpeg version: %s\nSource: %s\nSHA256: %s\n\n' "$FFMPEG_VERSION" "$FFMPEG_URL" "$FFMPEG_SHA256"
  printf 'LAME version: %s\nSource: %s\nSHA256: %s\n\n' "$LAME_VERSION" "$LAME_URL" "$LAME_SHA256"
  printf 'Target: x86_64, macOS %s+, generic CPU, no assembly optimizations\n' "$MACOS_MIN"
  printf 'Sources are unmodified. FFmpeg and LAME libraries are linked statically.\n'
  printf 'System frameworks/libraries remain dynamically linked.\n'
  printf 'License: LGPL 2.1 or later; see licenses and complete sources.\n\n'
  printf 'CFLAGS=%s\nLDFLAGS=%s\nSDK=%s\n\n' "$ENCODER_CFLAGS" "$ENCODER_LDFLAGS" "$ENCODER_SDK"
  printf 'LAME configure:'; printf ' %q' "${lame_configure[@]}"; printf '\n'
  printf 'FFmpeg configure:'; printf ' %q' "${ffmpeg_configure[@]}"; printf '\n\n'
  "$ENCODER_CC" --version
  xcrun --sdk macosx --show-sdk-version
  printf '\nBuilt: '; date -u '+%Y-%m-%dT%H:%M:%SZ'
  printf '\nThis package has not been installed, signed, uploaded or published by this script.\n'
} >"$ENCODER_DIST/BUILD.txt"
(
  cd "$ENCODER_DIST"
  shasum -a 256 ffmpeg sources/* licenses/* build-config/* BUILD.txt > SHA256SUMS
)

if [[ "$ENCODER_OUTPUT_DIR" != "$ENCODER_DIST" ]]; then
  mkdir -p "$(dirname "$ENCODER_OUTPUT_DIR")"
  mkdir "$ENCODER_OUTPUT_DIR" || fail "Output appeared during the build; choose a new destination."
  cp -R "$ENCODER_DIST/." "$ENCODER_OUTPUT_DIR/"
fi
printf 'Created local encoder package: %s\n' "$ENCODER_OUTPUT_DIR"
printf 'Build logs retained: %s\n' "$ENCODER_RUN_DIR"
printf 'Verify codec conversions and an actual Intel Mac before publishing.\n'

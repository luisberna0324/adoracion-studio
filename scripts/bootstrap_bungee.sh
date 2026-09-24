#!/usr/bin/env bash
set -euo pipefail

VERSION="${BUNGEE_VERSION:-2.4.24}"
ARCHIVE_SHA256="${BUNGEE_ARCHIVE_SHA256:-77eddfc55c0c3a7848ba81b23e46d134c12890b9cb318adcc68629f61db6e36f}"
LICENSE_SHA256="${BUNGEE_LICENSE_SHA256:-86cf9656479f1edb82245b985f4a2cc0d503b945766ebeda4269c60b307699ae}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${BUNGEE_DEST:-${ROOT}/third_party/bungee}"
ARCHIVE="${TMPDIR:-/tmp}/bungee-v${VERSION}.tgz"
URL="https://github.com/bungee-audio-stretch/bungee/releases/download/v${VERSION}/bungee-v${VERSION}.tgz"
LICENSE_URL="https://raw.githubusercontent.com/bungee-audio-stretch/bungee/v${VERSION}/LICENSE"

archive_targets() {
  case "$(uname -s)" in
    Darwin)
      printf '%s\n' include version.txt apple-mac apple-ios
      ;;
    Linux)
      case "$(uname -m)" in
        arm64|aarch64)
          printf '%s\n' include version.txt linux-aarch64
          ;;
        *)
          printf '%s\n' include version.txt linux-x86_64 android-arm64-v8a
          ;;
      esac
      ;;
    MINGW*|MSYS*|CYGWIN*)
      # The complete archive contains Apple framework symlinks. Windows tar
      # cannot reliably create them, and the Windows build does not need them.
      printf '%s\n' include version.txt windows-x86_64
      ;;
    *)
      echo "Unsupported platform for Bungee bootstrap: $(uname -s)" >&2
      return 1
      ;;
  esac
}

verify_sha256() {
  local expected="$1"
  local file="$2"
  if command -v shasum >/dev/null 2>&1; then
    echo "${expected}  ${file}" | shasum -a 256 -c -
  else
    echo "${expected}  ${file}" | sha256sum -c -
  fi
}

rm -rf "${DEST}"
mkdir -p "${DEST}"
curl -fL --retry 3 "${URL}" -o "${ARCHIVE}"
verify_sha256 "${ARCHIVE_SHA256}" "${ARCHIVE}"
TARGETS=()
while IFS= read -r target; do
  TARGETS+=("${target}")
done < <(archive_targets)
tar -xzf "${ARCHIVE}" -C "${DEST}" "${TARGETS[@]}"
curl -fL --retry 3 "${LICENSE_URL}" -o "${DEST}/LICENSE"
verify_sha256 "${LICENSE_SHA256}" "${DEST}/LICENSE"

test -f "${DEST}/include/bungee/Bungee.h"
test -f "${DEST}/version.txt"
test -f "${DEST}/LICENSE"
echo "Bungee ${VERSION} installed at ${DEST} (${TARGETS[*]})"

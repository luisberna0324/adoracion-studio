#!/usr/bin/env bash
set -euo pipefail

VERSION="${BUNGEE_VERSION:-2.4.24}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${BUNGEE_DEST:-${ROOT}/third_party/bungee}"
ARCHIVE="${TMPDIR:-/tmp}/bungee-v${VERSION}.tgz"
URL="https://github.com/bungee-audio-stretch/bungee/releases/download/v${VERSION}/bungee-v${VERSION}.tgz"

rm -rf "${DEST}"
mkdir -p "${DEST}"
curl -fL --retry 3 "${URL}" -o "${ARCHIVE}"
tar -xzf "${ARCHIVE}" -C "${DEST}"

test -f "${DEST}/include/bungee/Bungee.h"
echo "Bungee ${VERSION} installed at ${DEST}"

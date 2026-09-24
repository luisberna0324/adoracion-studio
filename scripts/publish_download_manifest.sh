#!/usr/bin/env bash
set -euo pipefail

: "${GCS_BUCKET:?GCS_BUCKET is required}"
: "${SEMVER:?SEMVER is required}"
[[ "${SEMVER}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo 'Invalid SEMVER' >&2; exit 1; }

prefix="${GCS_PREFIX:-installers}"
base="gs://${GCS_BUCKET}/${prefix}"
https_base="https://storage.googleapis.com/${GCS_BUCKET}/${prefix}"
version="v${SEMVER}"
mac="macos/${version}/playworship-macos-installer-${SEMVER}.pkg"
win="windows/${version}/playworship-windows-installer-${SEMVER}-x64.exe"
win32="windows/${version}/playworship-windows-installer-${SEMVER}-x86.exe"
android="android/${version}/playworship-android-${SEMVER}-arm64.apk"

for object in "${mac}" "${win}" "${win32}" "${android}"; do
  gcloud storage ls "${base}/${object}" >/dev/null
done

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
if gcloud storage cp "${base}/latest.json" "${work}/previous.json" >/dev/null 2>&1; then
  previous="$(jq -r '.version // "0.0.0"' "${work}/previous.json")"
  if [[ "$(printf '%s\n%s\n' "${SEMVER}" "${previous}" | sort -V | tail -n 1)" != "${SEMVER}" ]]; then
    echo "Skipping older release ${SEMVER}; latest is ${previous}"
    exit 0
  fi
fi

jq -n \
  --arg version "${SEMVER}" \
  --arg updatedAt "$(date -u +'%Y-%m-%dT%H:%M:%SZ')" \
  --arg macos "${https_base}/${mac}" \
  --arg windows "${https_base}/${win}" \
  --arg windowsX86 "${https_base}/${win32}" \
  --arg android "${https_base}/${android}" \
  '{version: $version, updatedAt: $updatedAt, platforms: {
    macos: {available: true, url: $macos, format: "pkg"},
    windows: {available: true, url: $windows, x86Url: $windowsX86, format: "exe"},
    android: {available: true, url: $android, format: "apk", architecture: "arm64"},
    ios: {available: false}
  }}' > "${work}/latest.json"

gcloud storage cp --cache-control='public,max-age=60' --content-type='application/json' \
  "${work}/latest.json" "${base}/latest.json"
echo "Published ${base}/latest.json for ${SEMVER}"

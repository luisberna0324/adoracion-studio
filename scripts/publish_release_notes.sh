#!/usr/bin/env bash
set -euo pipefail
: "${GCS_BUCKET:?GCS_BUCKET is required}"
: "${SEMVER:?SEMVER is required}"
[[ "$SEMVER" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo 'Invalid SEMVER' >&2; exit 1; }
[[ $# -eq 0 || ($# -eq 1 && "$1" == '--status-only') ]] || { echo 'Unknown publish mode' >&2; exit 1; }
base="gs://${GCS_BUCKET}/${GCS_PREFIX:-installers}/release-notes"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
arguments=(--version "$SEMVER" --output-dir "$work/rendered")
if gcloud storage cat "${base}/status/${SEMVER}.json" > "$work/previous-status.json" 2> "$work/status-error"; then
  arguments+=(--previous-status "$work/previous-status.json")
elif ! grep -Eiq '404|not found|matched no objects' "$work/status-error"; then
  cat "$work/status-error" >&2; exit 1
fi
status_only=false
if [[ "${1:-}" == '--status-only' ]]; then
  status_only=true
  arguments+=(--status-only)
else
  # Auth/network errors must never be treated as an empty history.
  if gcloud storage cat "${base}/index.json" > "$work/previous.json" 2> "$work/error"; then
    arguments+=(--previous-index "$work/previous.json")
  elif ! grep -Eiq '404|not found|matched no objects' "$work/error"; then
    cat "$work/error" >&2; exit 1
  fi
fi
python3 scripts/release_notes.py "${arguments[@]}"
upload() {
  gcloud storage cp --cache-control='public,max-age=60' --content-type='application/json' \
    "$work/rendered/$1" "${base}/$1"
}
# Publish individual files first, and only then expose them in the catalog.
upload "status/${SEMVER}.json"
if [[ "$status_only" == false ]]; then
  upload "versions/${SEMVER}.json"
  upload 'index.json'
  upload 'latest.json'
fi
echo "Published release ${SEMVER} (status-only=${status_only})"

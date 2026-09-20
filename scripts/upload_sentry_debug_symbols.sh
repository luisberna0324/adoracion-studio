#!/usr/bin/env bash
set -euo pipefail

# Intended for CI after a release artifact has been built. The script is a
# no-op when Sentry credentials are absent, so local/reproducible builds never
# need access to Sentry.
if [[ -z "${SENTRY_AUTH_TOKEN:-}" ]]; then
  echo "Sentry symbol upload skipped: SENTRY_AUTH_TOKEN is not configured."
  exit 0
fi

: "${SENTRY_ORG:=lujuma}"
: "${SENTRY_PROJECT:=playworship-app}"
: "${SENTRY_RELEASE:?SENTRY_RELEASE is required when uploading symbols}"

artifact_dir="${1:?Pass the release artifact directory}"
[[ -d "${artifact_dir}" ]] || { echo "Missing artifact directory: ${artifact_dir}" >&2; exit 1; }

export SENTRY_ORG SENTRY_PROJECT SENTRY_RELEASE
npx --yes @sentry/cli debug-files upload "${artifact_dir}"

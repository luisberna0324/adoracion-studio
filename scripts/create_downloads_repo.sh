#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/create_downloads_repo.sh --repo <owner/repo> [--description <text>]

Example:
  scripts/create_downloads_repo.sh \
    --repo tu-org/playworship-downloads \
    --description "Public installers and release assets for PlayWorship"
EOF
}

REPO=""
DESCRIPTION="Public installers and release assets"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --repo)
      REPO="${2:-}"
      shift 2
      ;;
    --description)
      DESCRIPTION="${2:-}"
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

if [[ -z "${REPO}" ]]; then
  echo "--repo is required" >&2
  usage
  exit 1
fi

if ! [[ "${REPO}" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]]; then
  echo "Invalid --repo format '${REPO}'. Expected owner/repo." >&2
  exit 1
fi

if ! command -v gh >/dev/null 2>&1; then
  echo "GitHub CLI (gh) is required." >&2
  exit 1
fi

if ! gh auth status >/dev/null 2>&1; then
  echo "Run 'gh auth login' first." >&2
  exit 1
fi

if gh repo view "${REPO}" >/dev/null 2>&1; then
  echo "Repo already exists: ${REPO}"
else
  gh repo create "${REPO}" --public --description "${DESCRIPTION}" --confirm
  echo "Created repo: ${REPO}"
fi

echo
echo "Next steps:"
echo "1) Create a PAT with repo scope for ${REPO}."
echo "2) Add PAT in this private repo as secret: DOWNLOADS_REPO_TOKEN."
echo "3) Run workflow 'Build Artifacts' with:"
echo "   - publish_release=true"
echo "   - publish_to_downloads_repo=true"
echo "   - downloads_repo=${REPO}"

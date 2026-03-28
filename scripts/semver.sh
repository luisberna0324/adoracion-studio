#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/semver.sh [options]

Options:
  --branch <name>                  Branch name to classify (feature/*, bugfix/*, hotfix/*, codex/*).
  --sequence <number>              Numeric suffix for prerelease (default: GITHUB_RUN_NUMBER or 0).
  --release-kind <auto|major|minor|patch>
                                   Version bump strategy (default: auto).
  --prerelease                     Emit SEMVER as prerelease (e.g. 1.2.0-feature.45).
  --require-supported-branch       Fail if branch does not match allowed prefixes in auto mode.
  -h, --help                       Show this help.
EOF
}

branch_name="${GITHUB_REF_NAME:-}"
sequence="${GITHUB_RUN_NUMBER:-0}"
release_kind="auto"
prerelease="false"
require_supported_branch="false"

while [[ $# -gt 0 ]]; do
  case "$1" in
  --branch)
    [[ $# -ge 2 ]] || {
      echo "Missing value for --branch" >&2
      exit 1
    }
    branch_name="$2"
    shift 2
    ;;
  --sequence)
    [[ $# -ge 2 ]] || {
      echo "Missing value for --sequence" >&2
      exit 1
    }
    sequence="$2"
    shift 2
    ;;
  --release-kind)
    [[ $# -ge 2 ]] || {
      echo "Missing value for --release-kind" >&2
      exit 1
    }
    release_kind="$2"
    shift 2
    ;;
  --prerelease)
    prerelease="true"
    shift
    ;;
  --require-supported-branch)
    require_supported_branch="true"
    shift
    ;;
  -h | --help)
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

branch_name="${branch_name#refs/heads/}"
if [[ -z "${branch_name}" ]]; then
  branch_name="$(git rev-parse --abbrev-ref HEAD 2>/dev/null || true)"
fi
if [[ -z "${branch_name}" ]]; then
  branch_name="unknown"
fi

if [[ ! "${sequence}" =~ ^[0-9]+$ ]]; then
  echo "Invalid --sequence value '${sequence}'. Expected a non-negative integer." >&2
  exit 1
fi

latest_tag="$(git tag --list 'v[0-9]*.[0-9]*.[0-9]*' --sort=-v:refname | head -n1 || true)"
if [[ -n "${latest_tag}" && "${latest_tag}" =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
  base_major="${BASH_REMATCH[1]}"
  base_minor="${BASH_REMATCH[2]}"
  base_patch="${BASH_REMATCH[3]}"
  last_stable="${base_major}.${base_minor}.${base_patch}"
else
  base_major=0
  base_minor=1
  base_patch=0
  last_stable="0.1.0"
  latest_tag=""
fi

classify_branch_kind() {
  local branch="$1"

  if [[ "${branch}" == feature/* ]]; then
    echo "feature"
    return
  fi

  if [[ "${branch}" == codex/* ]]; then
    # Transition rule: codex/* behaves like feature/*.
    echo "feature"
    return
  fi

  if [[ "${branch}" == bugfix/* ]]; then
    echo "bugfix"
    return
  fi

  if [[ "${branch}" == hotfix/* ]]; then
    echo "hotfix"
    return
  fi

  if [[ "${branch}" == main ]]; then
    echo "main"
    return
  fi

  echo "unsupported"
}

branch_kind="manual"
next_major="${base_major}"
next_minor="${base_minor}"
next_patch="${base_patch}"

case "${release_kind}" in
auto)
  branch_kind="$(classify_branch_kind "${branch_name}")"
  case "${branch_kind}" in
  feature)
    next_minor=$((base_minor + 1))
    next_patch=0
    ;;
  bugfix | hotfix)
    next_patch=$((base_patch + 1))
    ;;
  main)
    # Keep stable version as-is for main CI builds.
    ;;
  unsupported)
    if [[ "${require_supported_branch}" == "true" ]]; then
      echo "Unsupported source branch '${branch_name}'. Allowed prefixes: feature/*, bugfix/*, hotfix/*, codex/*." >&2
      exit 1
    fi
    branch_kind="dev"
    ;;
  *)
    echo "Unexpected branch classification: ${branch_kind}" >&2
    exit 1
    ;;
  esac
  ;;
major)
  branch_kind="major"
  next_major=$((base_major + 1))
  next_minor=0
  next_patch=0
  ;;
minor)
  branch_kind="minor"
  next_minor=$((base_minor + 1))
  next_patch=0
  ;;
patch)
  branch_kind="patch"
  next_patch=$((base_patch + 1))
  ;;
*)
  echo "Invalid --release-kind '${release_kind}'. Use auto|major|minor|patch." >&2
  exit 1
  ;;
esac

next_stable="${next_major}.${next_minor}.${next_patch}"
tag="v${next_stable}"
semver="${next_stable}"

if [[ "${prerelease}" == "true" ]]; then
  suffix_kind="${branch_kind}"
  if [[ "${suffix_kind}" == "main" ]]; then
    suffix_kind="main"
  elif [[ "${suffix_kind}" == "manual" ]]; then
    suffix_kind="dev"
  fi
  semver="${next_stable}-${suffix_kind}.${sequence}"
fi

emit_output() {
  local key="$1"
  local value="$2"
  printf '%s=%s\n' "${key}" "${value}"
  if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
    printf '%s=%s\n' "${key}" "${value}" >>"${GITHUB_OUTPUT}"
  fi
}

emit_output "SEMVER" "${semver}"
emit_output "NEXT_STABLE" "${next_stable}"
emit_output "TAG" "${tag}"
emit_output "BRANCH_KIND" "${branch_kind}"
emit_output "LAST_STABLE" "${last_stable}"

# Lowercase aliases for workflow ergonomics. un ajuste para el bugfix
emit_output "semver" "${semver}"
emit_output "next_stable" "${next_stable}"
emit_output "tag" "${tag}"
emit_output "branch_kind" "${branch_kind}"
emit_output "last_stable" "${last_stable}"

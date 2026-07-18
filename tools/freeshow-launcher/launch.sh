#!/usr/bin/env bash
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export FREESHOW_HOST="${FREESHOW_HOST:-127.0.0.1}"
export FREESHOW_PORT="${FREESHOW_PORT:-5506}"
export PORT="${PORT:-8787}"

node "$DIR/server.mjs"

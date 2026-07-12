#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ADMIN_HOST="${ANIMUS_ADMIN_HOST:-127.0.0.1}"
ADMIN_PORT="${ANIMUS_ADMIN_PORT:-8080}"
SKIP_UI_BUILD="${ANIMUS_SKIP_UI_BUILD:-0}"

if [[ "${SKIP_UI_BUILD}" != "1" ]]; then
  "${ROOT_DIR}/scripts/build-ui.sh"
fi

"${ROOT_DIR}/scripts/start-daemon.sh"

echo "Open: http://${ADMIN_HOST}:${ADMIN_PORT}/"

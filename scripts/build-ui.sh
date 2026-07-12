#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UI_DIR="${ROOT_DIR}/admin-ui"

if [[ ! -d "${UI_DIR}" ]]; then
  echo "admin-ui directory not found at ${UI_DIR}" >&2
  exit 1
fi

cd "${UI_DIR}"

if [[ ! -d node_modules ]]; then
  npm install
fi

npm run build

echo "UI build complete: ${UI_DIR}/dist"
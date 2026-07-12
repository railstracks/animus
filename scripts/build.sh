#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

# Build the admin UI first so CMake can find the dist output for embedding.
echo "=== Building admin UI ==="
"${ROOT_DIR}/scripts/build-ui.sh"

echo "=== Cleaning stale embedded resources ==="
rm -f "${BUILD_DIR}/generated/AdminUiEmbeddedResources.cpp"

echo "=== Configuring CMake ==="
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
  -DANIMUS_ADMIN_UI_EMBED=ON \
  -DANIMUS_ADMIN_UI_BUILD=OFF

echo "=== Building animus ==="
cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "=== Build complete ==="
echo "Binary: ${ROOT_DIR}/dist/bin/animusd"

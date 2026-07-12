#!/usr/bin/env bash
set -euo pipefail

# ============================================================================
# build-animus-docker-sudo.sh — build Docker image with passwordless sudo
#
# Same as the standard image, but the animus user has passwordless sudo.
# For agents that need to modify their host OS (install packages, manage
# services, configure networking, etc.).
#
# Usage:
#   ./build-animus-docker-sudo.sh [tag]
#
# Environment variables:
#   REGISTRY_USER  Docker Hub / registry username (default: mrsommer)
#   IMAGE_NAME     Image name (default: animus-sudo)
#   PUSH           Push after build (default: 0)
#   WITH_MODEL     Include embedding model (default: 0, set to 1 to include)
# ============================================================================

REGISTRY_USER="${REGISTRY_USER:-mrsommer}"
IMAGE_NAME="${IMAGE_NAME:-animus-sudo}"
TAG="${1:-latest}"
PUSH="${PUSH:-0}"
WITH_MODEL="${WITH_MODEL:-0}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

IMAGE="${REGISTRY_USER}/${IMAGE_NAME}:${TAG}"

echo "--------------------------------------------------"
echo "Building ${IMAGE}"
echo "Source:  ${ROOT_DIR}"
echo "Model:   ${WITH_MODEL}"
echo "--------------------------------------------------"

# Build the binary and admin UI first on the host (requires build deps)
echo "=== Building Animus binary + admin UI ==="
"${ROOT_DIR}/scripts/build.sh"

# Prepare build context
CONTEXT_DIR="$(mktemp -d)"
trap 'rm -rf "${CONTEXT_DIR}"' EXIT

mkdir -p "${CONTEXT_DIR}/dist/bin"
mkdir -p "${CONTEXT_DIR}/dist/lib"
mkdir -p "${CONTEXT_DIR}/lua/_shared"

# Copy the binary
cp "${ROOT_DIR}/dist/bin/animusd" "${CONTEXT_DIR}/dist/bin/"

# Copy shared libraries that animusd depends on (custom-built ones)
BUILD_DIR="${ROOT_DIR}/build"
DROGON_DIR="${BUILD_DIR}/_deps/drogon-build"
TRANTOR_DIR="${DROGON_DIR}/trantor"
JSONCPP_DIR="${BUILD_DIR}/_deps/jsoncpp-build/src/lib_json"
GGML_DIR="${BUILD_DIR}/bin"

for lib in \
    "${ROOT_DIR}/dist/libanimus_kernel.so" \
    "${DROGON_DIR}/libdrogon.so.1" \
    "${TRANTOR_DIR}/libtrantor.so.1" \
    "${JSONCPP_DIR}/libjsoncpp.so.26" \
    "${GGML_DIR}/libllama.so.0" \
    "${GGML_DIR}/libggml.so.0" \
    "${GGML_DIR}/libggml-cpu.so.0" \
    "${GGML_DIR}/libggml-base.so.0"; do
    if [[ -f "${lib}" ]]; then
        cp "${lib}" "${CONTEXT_DIR}/dist/lib/"
    else
        echo "⚠️  Warning: library not found: ${lib}"
    fi
done

# Copy Lua scripts
if [[ -d "${ROOT_DIR}/state/lua/_shared" ]]; then
    cp "${ROOT_DIR}/state/lua/_shared/"*.lua "${CONTEXT_DIR}/lua/_shared/" 2>/dev/null || true
fi

# Optionally include embedding model
if [[ "${WITH_MODEL}" == "1" ]]; then
    mkdir -p "${CONTEXT_DIR}/models"
    if [[ -f "${ROOT_DIR}/models/embeddinggemma-300m-qat-Q8_0.gguf" ]]; then
        cp "${ROOT_DIR}/models/embeddinggemma-300m-qat-Q8_0.gguf" "${CONTEXT_DIR}/models/"
    else
        echo "⚠️  WITH_MODEL=1 but model file not found, skipping"
    fi
fi

# Always create models dir in context so COPY doesn't fail.
# The actual model file is only copied when WITH_MODEL=1.
mkdir -p "${CONTEXT_DIR}/models"

# Write Dockerfile into context
cat > "${CONTEXT_DIR}/Dockerfile" << 'DOCKERFILE'
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Runtime dependencies (system packages only)
RUN apt-get update && apt-get install -y --no-install-recommends \
    libcurl4 \
    libsqlite3-0 \
    libssl3 \
    libsodium23 \
    libpq5 \
    libz1 \
    libuuid1 \
    libbrotli1 \
    libgomp1 \
    ca-certificates \
    curl \
    gh \
    && rm -rf /var/lib/apt/lists/*

# Create animus user
RUN useradd --create-home --shell /bin/bash animus

# Create directories and set ownership
RUN mkdir -p /data/config /data/data/lua/_shared /data/data/log /data/data/run /data/data/memory && \
    chown -R animus:animus /data

# Copy binary and libraries
COPY dist/bin/animusd /usr/local/bin/animusd
COPY dist/lib/ /usr/local/lib/
RUN ldconfig

# Copy Lua scripts to default data dir
COPY --chown=animus:animus lua/_shared/ /data/data/lua/_shared/

# Model directory — empty by default, populated when WITH_MODEL=1
COPY --chown=animus:animus models/ /data/data/models/

# Passwordless sudo for the animus user
RUN apt-get update && apt-get install -y --no-install-recommends sudo && \
    echo "animus ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/animus && \
    chmod 0440 /etc/sudoers.d/animus && \
    rm -rf /var/lib/apt/lists/*

# Environment variables for database configuration
# If DB_HOST is set and config/db.json doesn't exist, the entrypoint
# generates a PostgreSQL config. Otherwise SQLite is used.
# If config/db.json is mounted, env vars are ignored.
#
# ANIMUS_SKIP_MODEL_DOWNLOAD=1 to skip auto-downloading the embedding model
# ADMIN_HOST / ADMIN_PORT to override the admin server bind address/port
ENV DB_HOST=""
ENV DB_NAME=""
ENV DB_USER=""
ENV DB_PASS=""
ENV DB_PORT="5432"
ENV ADMIN_HOST="0.0.0.0"
ENV ADMIN_PORT="8080"
ENV ANIMUS_SKIP_MODEL_DOWNLOAD=""
ENV HOME="/home/animus"

# Write entrypoint script
COPY <<'ENTRYPOINT' /usr/local/bin/animus-entrypoint.sh
#!/usr/bin/env bash
set -euo pipefail

CONFIG_DIR="/data/config"
DB_CONFIG="${CONFIG_DIR}/db.json"

mkdir -p "${CONFIG_DIR}" /data/data/lua/_shared /data/data/log /data/data/run /data/data/memory

# Generate db.json from environment variables if no config is mounted
if [ ! -f "${DB_CONFIG}" ]; then
    if [ -n "${DB_HOST}" ] && [ -n "${DB_NAME}" ] && [ -n "${DB_USER}" ]; then
        echo "=== Generating PostgreSQL config from environment ==="
        cat > "${DB_CONFIG}" << EOF
{
  "backend": "postgresql",
  "postgresql": {
    "host": "${DB_HOST}",
    "port": ${DB_PORT:-5432},
    "database": "${DB_NAME}",
    "username": "${DB_USER}",
    "password": "${DB_PASS:-}",
    "pool_size": 10
  }
}
EOF
    fi
fi

echo "=== Starting Animus ==="

# In Docker, bind to all interfaces by default so port forwarding works.
# Override with -e ADMIN_HOST=... if needed.
ADMIN_HOST="${ADMIN_HOST:-0.0.0.0}"
ADMIN_PORT="${ADMIN_PORT:-8080}"

exec animusd --config-dir /data/config --data-dir /data/data \
    --admin-host "${ADMIN_HOST}" --admin-port "${ADMIN_PORT}"
ENTRYPOINT
RUN chmod +x /usr/local/bin/animus-entrypoint.sh

VOLUME ["/data", "/home/animus"]

USER animus
WORKDIR /home/animus

EXPOSE 8080

ENTRYPOINT ["/usr/local/bin/animus-entrypoint.sh"]
DOCKERFILE

echo "=== Building Docker image ==="
docker build -t "${IMAGE}" "${CONTEXT_DIR}"

if [[ "${PUSH}" == "1" ]]; then
    docker push "${IMAGE}"
    echo "✅ Pushed ${IMAGE}"
else
    echo "ℹ️  PUSH=${PUSH}; skipping push"
fi

echo "--------------------------------------------------"
echo "Done."
echo "Image: ${IMAGE}"
echo ""
echo "Run (SQLite, zero-config):"
echo "  docker run -p 8080:8080 -v animus-data:/data -v animus-home:/home/animus ${IMAGE}"
echo ""
echo "Run (PostgreSQL via env vars):"
echo "  docker run -p 8080:8080 -v animus-data:/data -v animus-home:/home/animus \\"
echo "    -e DB_HOST=postgres-host \\"
echo "    -e DB_NAME=animus \\"
echo "    -e DB_USER=animus \\"
echo "    -e DB_PASS=secret \\"
echo "    ${IMAGE}"
echo ""
echo "Run (custom config, mount your own db.json):"
echo "  docker run -p 8080:8080 -v animus-data:/data -v animus-home:/home/animus \\"
echo "    -v /path/to/config:/data/config ${IMAGE}"
echo "--------------------------------------------------"

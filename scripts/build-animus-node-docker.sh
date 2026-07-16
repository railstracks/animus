#!/usr/bin/env bash
set -euo pipefail

# ============================================================================
# build-animus-node-docker.sh — build Docker image for Animus node client
#
# This image contains an Animus binary configured to run in --node mode.
# It connects to a central Animus server on startup and executes tool
# calls (shell_exec, file) on behalf of the server.
#
# Usage:
#   ./build-animus-node-docker.sh [tag]
#
# Environment variables:
#   REGISTRY_USER  Docker Hub / registry username (default: mrsommer)
#   IMAGE_NAME     Image name (default: animus-node)
#   PUSH           Push after build (default: 0)
#
# The resulting image is configured via environment variables at runtime:
#   SERVER_URL     WebSocket URL of the Animus server (e.g. ws://host:8080/ws/node)
#   NODE_TOKEN     Authentication token for the server
#   NODE_NAME      Name to register as (e.g. "workstation", "raspberry-pi")
#   NODE_TOOLS     Comma-separated list of tools to enable (default: exec,file)
# ============================================================================

REGISTRY_USER="${REGISTRY_USER:-mrsommer}"
IMAGE_NAME="${IMAGE_NAME:-animus-node}"
TAG="${1:-latest}"
PUSH="${PUSH:-0}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

IMAGE="${REGISTRY_USER}/${IMAGE_NAME}:${TAG}"

echo "--------------------------------------------------"
echo "Building ${IMAGE} (node client)"
echo "Source:  ${ROOT_DIR}"
echo "--------------------------------------------------"

# Build the binary first on the host (requires build deps)
echo "=== Building Animus binary ==="
"${ROOT_DIR}/scripts/build.sh"

# Prepare build context
CONTEXT_DIR="$(mktemp -d)"
trap 'rm -rf "${CONTEXT_DIR}"' EXIT

mkdir -p "${CONTEXT_DIR}/dist/bin"
mkdir -p "${CONTEXT_DIR}/dist/lib"

# Copy the binary
cp "${ROOT_DIR}/dist/bin/animusd" "${CONTEXT_DIR}/dist/bin/"

# Copy shared libraries that animusd depends on (custom-built ones)
BUILD_DIR="${ROOT_DIR}/build"
DROGON_DIR="${BUILD_DIR}/_deps/drogon-build"
TRANTOR_DIR="${DROGON_DIR}/trantor"
JSONCPP_DIR="${BUILD_DIR}/_deps/jsoncpp-build/src/lib_json"

for lib in \
    "${ROOT_DIR}/dist/libanimus_kernel.so" \
    "${DROGON_DIR}/libdrogon.so.1" \
    "${TRANTOR_DIR}/libtrantor.so.1" \
    "${JSONCPP_DIR}/libjsoncpp.so.26"; do
    if [[ -f "${lib}" ]]; then
        cp "${lib}" "${CONTEXT_DIR}/dist/lib/"
    else
        echo "⚠️  Warning: library not found: ${lib}"
    fi
done

# Write Dockerfile into context
cat > "${CONTEXT_DIR}/Dockerfile" << 'DOCKERFILE'
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Runtime dependencies (node client only needs curl + core libs)
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
    git \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Copy binary and libraries
COPY dist/bin/animusd /usr/local/bin/animusd
COPY dist/lib/ /usr/local/lib/
RUN ldconfig

# Environment variables for node configuration
# Required: SERVER_URL, NODE_TOKEN, NODE_NAME
# Optional: NODE_TOOLS (default: exec,file)
ENV SERVER_URL=""
ENV NODE_TOKEN=""
ENV NODE_NAME=""
ENV NODE_TOOLS="exec,file"

# Write entrypoint script
COPY <<'ENTRYPOINT' /usr/local/bin/animus-node-entrypoint.sh
#!/usr/bin/env bash
set -euo pipefail

# Validate required env vars
if [ -z "${SERVER_URL}" ]; then
    echo "ERROR: SERVER_URL is required (e.g. ws://animus-host:8080/ws/node)"
    exit 1
fi
if [ -z "${NODE_TOKEN}" ]; then
    echo "ERROR: NODE_TOKEN is required"
    exit 1
fi
if [ -z "${NODE_NAME}" ]; then
    echo "ERROR: NODE_NAME is required (e.g. workstation, raspberry-pi)"
    exit 1
fi

echo "=== Starting Animus Node Client ==="
echo "  Server: ${SERVER_URL}"
echo "  Name:   ${NODE_NAME}"
echo "  Tools:  ${NODE_TOOLS}"
echo "==================================="

exec animusd --node \
    --server-url "${SERVER_URL}" \
    --token "${NODE_TOKEN}" \
    --node-name "${NODE_NAME}" \
    --node-tools "${NODE_TOOLS}"
ENTRYPOINT
RUN chmod +x /usr/local/bin/animus-node-entrypoint.sh

# No volume needed — node client is stateless
# No port exposure — node client makes outbound connections only

ENTRYPOINT ["/usr/local/bin/animus-node-entrypoint.sh"]
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
echo "Run (connect to Animus server):"
echo "  docker run --rm \\"
echo "    -e SERVER_URL=ws://animus-host:8080/ws/node \\"
echo "    -e NODE_TOKEN=your-secret-token \\"
echo "    -e NODE_NAME=workstation \\"
echo "    ${IMAGE}"
echo ""
echo "Run with custom tool list:"
echo "  docker run --rm \\"
echo "    -e SERVER_URL=ws://animus-host:8080/ws/node \\"
echo "    -e NODE_TOKEN=your-secret-token \\"
echo "    -e NODE_NAME=workstation \\"
echo "    -e NODE_TOOLS=exec,file \\"
echo "    ${IMAGE}"
echo ""
echo "Run with host network (for local Animus server):"
echo "  docker run --rm --network host \\"
echo "    -e SERVER_URL=ws://localhost:8080/ws/node \\"
echo "    -e NODE_TOKEN=your-secret-token \\"
echo "    -e NODE_NAME=docker-node \\"
echo "    ${IMAGE}"
echo "--------------------------------------------------"
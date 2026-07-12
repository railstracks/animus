#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN_PATH="${ROOT_DIR}/dist/bin/animusd"
RUN_DIR="${ROOT_DIR}/dist/run"
LOG_DIR="${ROOT_DIR}/dist/log"
PID_FILE="${RUN_DIR}/animusd.pid"
LOG_FILE="${LOG_DIR}/animusd.log"

ADMIN_HOST="${ANIMUS_ADMIN_HOST:-127.0.0.1}"
ADMIN_PORT="${ANIMUS_ADMIN_PORT:-8080}"
ADMIN_ENABLED="${ANIMUS_ADMIN_ENABLED:-1}"

mkdir -p "${RUN_DIR}" "${LOG_DIR}"

if [[ ! -x "${BIN_PATH}" ]]; then
  echo "animusd binary not found at ${BIN_PATH}. Run scripts/build.sh first." >&2
  exit 1
fi

if [[ -f "${PID_FILE}" ]]; then
  EXISTING_PID="$(cat "${PID_FILE}")"
  if [[ -n "${EXISTING_PID}" ]] && kill -0 "${EXISTING_PID}" 2>/dev/null; then
    echo "animusd is already running (pid ${EXISTING_PID})."
    exit 0
  fi
  rm -f "${PID_FILE}"
fi

ARGS=("--daemon" "--admin-host" "${ADMIN_HOST}" "--admin-port" "${ADMIN_PORT}")
if [[ "${ADMIN_ENABLED}" == "0" ]]; then
  ARGS+=("--disable-admin")
fi

nohup "${BIN_PATH}" "${ARGS[@]}" >"${LOG_FILE}" 2>&1 < /dev/null &
PID=$!
echo "${PID}" > "${PID_FILE}"

sleep 0.5
if ! kill -0 "${PID}" 2>/dev/null; then
  echo "Failed to start animusd. Check ${LOG_FILE}" >&2
  rm -f "${PID_FILE}"
  exit 1
fi

echo "animusd started (pid ${PID}). Log: ${LOG_FILE}"
if [[ "${ADMIN_ENABLED}" != "0" ]]; then
  echo "Admin UI/API available at: http://${ADMIN_HOST}:${ADMIN_PORT}/"
fi

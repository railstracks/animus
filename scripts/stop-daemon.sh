#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUN_DIR="${ROOT_DIR}/dist/run"
PID_FILE="${RUN_DIR}/animusd.pid"

if [[ ! -f "${PID_FILE}" ]]; then
  echo "animusd is not running (no pid file)."
  exit 0
fi

PID="$(cat "${PID_FILE}")"
if [[ -z "${PID}" ]]; then
  rm -f "${PID_FILE}"
  echo "animusd pid file was empty; cleaned up."
  exit 0
fi

if ! kill -0 "${PID}" 2>/dev/null; then
  rm -f "${PID_FILE}"
  echo "animusd process ${PID} not found; cleaned up stale pid file."
  exit 0
fi

kill -TERM "${PID}" 2>/dev/null || true
for _ in $(seq 1 30); do
  if ! kill -0 "${PID}" 2>/dev/null; then
    rm -f "${PID_FILE}"
    echo "animusd stopped."
    exit 0
  fi
  sleep 0.2
done

kill -KILL "${PID}" 2>/dev/null || true
sleep 0.2
if kill -0 "${PID}" 2>/dev/null; then
  echo "Failed to stop animusd (pid ${PID})." >&2
  exit 1
fi

rm -f "${PID_FILE}"
echo "animusd force-stopped."
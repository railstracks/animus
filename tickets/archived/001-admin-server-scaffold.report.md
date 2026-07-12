# Ticket 001 Report — AdminServer Subsystem Scaffold

## Summary
Ticket `001-admin-server-scaffold` has been implemented with an embedded drogon-backed admin subsystem integrated into `AgentKernel` lifecycle.

## Implemented Scope
- Added drogon as a CMake dependency (FetchContent-based by default), with optional system-package mode.
- Added `AdminServer` subsystem class and linked it into the kernel.
- Wired lifecycle integration:
  - `AgentKernel::Start()` starts admin server when enabled.
  - `AgentKernel::Stop()` stops admin server cleanly before kernel teardown.
- Added configurable host/port/enabled flags in kernel config:
  - `admin.enabled`
  - `admin.host`
  - `admin.port`
- Implemented health endpoint:
  - `GET /api/v1/status` returns JSON with `status` and `uptime`.
- Added basic static placeholder routes:
  - `GET /`
  - `GET /static/admin.css`
- Added CLI flags for local runtime configuration/testing:
  - `--run-kernel`
  - `--admin-host <host>`
  - `--admin-port <port>`
  - `--disable-admin`

## Validation Per Acceptance Criteria
- Animus starts an HTTP server on configured port: ✅
- `GET /api/v1/status` returns expected JSON structure: ✅
- Server stops cleanly on kernel shutdown: ✅
- Server can be disabled by config without build changes: ✅
- Builds on Linux in this environment (GCC 13): ✅

## Tests Added
- `tests/AdminServerTests.cpp`
  - verifies status endpoint behavior
  - verifies shutdown behavior (port no longer accepting connections)
  - verifies disabled mode (port not opened)

## Notes
- Build wiring includes vendored `jsoncpp` to ensure consistent drogon configuration in clean dev environments.
- No external reverse proxy is required; server is embedded in-process as designed.
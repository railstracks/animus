# Ticket 001 — AdminServer Subsystem Scaffold

**Priority:** P0 (blocks all other admin work)
**Status:** Open
**Dependencies:** None

## Summary

Integrate drogon as an embedded HTTP/WebSocket server within the Animus process. The AdminServer runs as a kernel subsystem alongside existing session and module management.

## Scope

- Add drogon as a CMake dependency (FetchContent or find_package)
- Create `AdminServer` class that owns a drogon application instance
- Register AdminServer as a kernel subsystem (start/stop lifecycle)
- Bind to configurable host:port (default `127.0.0.1:8080`)
- Health check endpoint: `GET /api/v1/status` returning JSON with agent status, uptime
- Basic static file serving route (placeholder for future SPA)
- Configuration via existing config system (host, port, enabled flag)

## Acceptance Criteria

- [ ] Animus binary starts drogon HTTP server on configured port
- [ ] `GET /api/v1/status` returns `{ "status": "ok", "uptime": <seconds> }`
- [ ] Server stops cleanly on kernel shutdown
- [ ] Server can be disabled via config without build changes
- [ ] Builds on Linux (GCC 12+, Clang 15+)

## Notes

- drogon's CMake integration should use FetchContent for now; packaging can come later
- Consider thread model: drogon uses its own thread pool; ensure no conflict with JobSystem workers
- Config key: `admin.host`, `admin.port`, `admin.enabled`

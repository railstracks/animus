# Ticket 044 — Scheduler Legibility in Admin UI: Implementation Report

**Status:** CLOSED  
**Completed:** 2026-05-10

## Summary

Delivered a first-class scheduler control surface in admin by adding scheduler API endpoints and a dedicated UI page. The scheduler is now visible and inspectable without manual SQLite/log inspection.

## Implemented

### 1) Backend scheduler endpoints

Added read/control endpoints:

- `GET /api/v1/scheduler/status`
- `GET /api/v1/scheduler/schedules?agent_id=<id>&tag=<optional>&include_disabled=<bool>`
- `GET /api/v1/scheduler/schedules/{id}`
- `DELETE /api/v1/scheduler/schedules/{id}`

Implementation points:

- `AdminServer` now receives scheduler pointer from kernel startup wiring.
- Routes return normalized schedule payload shape (`schedule_type`, `next_fire`, `fire_count`, `max_fires`, etc.).

Files:

- `include/animus_kernel/AdminServer.h`
- `src/kernel/AgentKernel.cpp`
- `src/kernel/admin/internal/AdminServerRoutesAgentsAndRuntime.inc`
- `src/kernel/admin/AdminServerRoutes.cpp`

### 2) Scheduler page in admin UI

Added dedicated page with:

- runtime status card (`running` / `stopped`)
- agent selector
- optional tag filter
- include-disabled toggle
- schedule table:
  - id
  - type
  - tag
  - next fire (human + raw)
  - state chips (`scheduled` / `due` / `disabled`)
  - fire count (+ max fires)
  - cancel action

Files:

- `admin-ui/src/views/SchedulerView.vue`
- `admin-ui/src/router/index.ts`
- `admin-ui/src/components/AppSidebar.vue`
- `admin-ui/src/i18n/locales/en.ts`
- `admin-ui/src/i18n/locales/nl.ts`

### 3) Integration tests

Extended admin API test coverage for scheduler endpoints:

- status endpoint contract
- schedules list contract
- required `agent_id` validation

File:

- `tests/AdminServerTests.cpp`

## Acceptance Criteria Check

- [x] Admin API exposes scheduler status and list endpoints
- [x] Admin API supports schedule cancellation by id
- [x] UI has `/scheduler` route and sidebar navigation entry
- [x] UI supports agent/tag filtering
- [x] UI makes due/disabled state visible at a glance
- [x] Admin integration test includes scheduler endpoint coverage

## Validation

- `cmake --build build -j4` passed
- `ctest -R "AdminServerTests" --output-on-failure` passed
- `cd admin-ui && npm run build` passed

## Notes

- This ticket focused on legibility + operational controls, not schedule creation/edit UX.
- Follow-up can add richer scheduler authoring (create/update forms, grouping, run-history drill-down).

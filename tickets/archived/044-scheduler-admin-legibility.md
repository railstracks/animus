# Ticket 044 - Scheduler Legibility in Admin UI

**Priority:** P1 (operability / debugging)
**Status:** CLOSED
**Dependencies:** Ticket 027 (Scheduler), Ticket 035 (AdminServer decomposition)
**Updated:** 2026-05-10

## Summary

Add a dedicated Scheduler page in the Admin UI and expose scheduler-focused admin API routes so operators can inspect schedule state without digging through SQLite or logs.

## Motivation

The scheduler is now a critical subsystem for consolidation and other autonomous behavior, but it is not currently legible in the control surface:

- No scheduler navigation entry in the admin UI.
- No scheduler endpoint in admin API for status and schedule listing.
- No fast way to answer basic questions:
  - Is the scheduler running?
  - What schedules are registered for an agent?
  - Which schedules are due soon or already overdue?
  - How many times has a schedule fired?

This slows diagnosis when consolidation appears idle or unexpectedly noisy.

## Scope

### Backend API

Add scheduler routes:

- `GET /api/v1/scheduler/status`
  - Returns scheduler process state (`running`).
- `GET /api/v1/scheduler/schedules?agent_id=<id>&tag=<optional>&include_disabled=<bool>`
  - Returns per-agent schedules with fire metadata.
- `GET /api/v1/scheduler/schedules/{id}`
  - Returns one schedule by id.
- `DELETE /api/v1/scheduler/schedules/{id}`
  - Cancels one schedule by id.

### Admin UI

Add `SchedulerView` with:

- Header + refresh action.
- Status card (`running`/`stopped`).
- Filter controls:
  - Agent selector
  - Tag filter
  - Include disabled toggle
- Schedule table:
  - ID
  - Type (one-shot/recurring)
  - Tag
  - Next fire (human + ISO)
  - Status (scheduled/due/disabled)
  - Fire count (`fire_count` and optional `max_fires`)
  - Cancel action

Navigation updates:

- New sidebar entry: **Scheduler**
- New route: `/scheduler`

## Acceptance Criteria

- [ ] Admin API exposes scheduler status and schedule listing endpoints.
- [ ] Admin API supports canceling schedules by id.
- [ ] UI includes `/scheduler` page wired from sidebar navigation.
- [ ] Scheduler page supports filtering by agent and tag.
- [ ] Scheduler page makes due/disabled state legible at a glance.
- [ ] Admin server integration test covers new scheduler endpoints.

## Notes

- This ticket focuses on legibility and control, not advanced schedule editing UX.
- Follow-up ticket can add schedule creation/edit forms in admin UI if needed.

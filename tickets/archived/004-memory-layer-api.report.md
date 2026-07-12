# Ticket 004 Report — Memory Layer API

## Summary
Ticket `004-memory-layer-api` has been implemented with REST endpoints for memory layer browsing, observation inspection/metadata mutation, archive/removal behavior, and async consolidation orchestration via the JobSystem.

## Implemented Scope
- `GET /api/v1/memory/layers`
  - Returns configured layers with metadata:
  - `name`, `description`, `retention_policy`, `observation_count`
  - `horizon`, `consolidation_interval`
  - `perspective_past`, `perspective_current`, `perspective_future`
- `GET /api/v1/memory/layers/:name/observations`
  - Returns paginated observations for a layer.
  - Supports `sort=weight|age`, `order=asc|desc`.
  - Pagination defaults/clamps:
  - `page=1` default, minimum 1
  - `limit=50` default, max 200
- `GET /api/v1/memory/observations/:id`
  - Returns full observation metadata including:
  - `id`, `text`, `content`, `tags`, `weight`, `decay_rate`, `layer`, `created_in_layer`, `age_seconds`
  - timestamps (`timestamp`, `timestamp_unix_ms`, `demotion_timestamp`, `demotion_timestamp_unix_ms`)
  - demotion metadata (`demotion_reason`)
- `PATCH /api/v1/memory/observations/:id`
  - Partial metadata updates for `tags` and/or `weight`.
  - Fields not provided remain unchanged.
- `DELETE /api/v1/memory/observations/:id`
  - Archives observation by moving it to `layer="archive"`.
  - Records demotion metadata (`demotion_reason="removed_via_api"`, demotion timestamp).
- `POST /api/v1/memory/consolidate`
  - Returns immediately with a `job_id`.
  - Schedules consolidation work on `jobs::JobLane::Memory`.
- `GET /api/v1/memory/consolidation/status`
  - Reports state (`idle|running`), active/last job IDs, last run timestamp,
    last summary (`promoted`, `demoted`, `archived`), and last error.

## Persistence and Compatibility
- Memory persistence is backed by `KernelConfig::MemoryConfig` and file storage at `memory.filePath`.
- Layer definitions and observation data are loaded/saved in a JSON state file.
- Consolidation state is persisted and restored.
- Job ID continuity across restarts is preserved from persisted consolidation state.
- Observation payload compatibility supports both legacy and explicit timestamp keys:
  - `timestamp` and `timestamp_unix_ms`
  - `demotion_timestamp` and `demotion_timestamp_unix_ms`

## Validation Against Acceptance Criteria
- Layer list reflects configurable temporal memory structure: ✅
- Layer metadata includes horizon, consolidation interval, and perspective fields: ✅
- Observation pagination defaults/max behavior (`50`, `200`) works: ✅

- Observation detail includes full metadata fields from observation format expectations: ✅
- PATCH supports partial updates without clobbering untouched fields: ✅

## Tests
`tests/AdminServerTests.cpp` was extended with end-to-end memory API checks for:
- layer listing and per-layer counts
- observation pagination/sorting/limit clamp
- observation detail metadata coverage
- PATCH partial update semantics for tags/weight
- DELETE archive behavior and demotion metadata
- consolidation trigger and status lifecycle (`job_id`, last run, summary)

All tests pass in the current suite.
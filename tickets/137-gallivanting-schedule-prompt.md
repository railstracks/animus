# Ticket 137 — Gallivanting Schedule Prompt Storage

## Problem

When a gallivanting schedule fires, the entire JSON config blob is passed as the agent's prompt message:

```json
{"type":"gallivanting","duration_minutes":60,"random_offset_minutes":30,"prompt_template":"# Gallivanting Block\n..."}
```

The agent receives this raw JSON instead of the extracted `prompt_template` content. The scheduler stores the `message` field verbatim from the schedule config, and AgentKernel passes it directly to the session.

## Root Cause

The `ScheduleDescriptor.message` field stores whatever was configured — there's no special handling for gallivanting-type schedules. The JSON blob gets passed through as-is.

## Proposed Fix

Store the gallivanting prompt directly on the schedule's `message` field when creating the schedule, rather than storing the full JSON config. Two approaches:

1. **Admin UI layer:** When creating a gallivanting schedule via the UI/API, parse the JSON config, extract `prompt_template`, and store that as the `message`. Store the other config fields (`duration_minutes`, `random_offset_minutes`, `type`) as schedule metadata or separate columns.

2. **AgentKernel layer:** When a gallivanting session fires, if the `message` looks like JSON with a `prompt_template` field, extract it before passing to the session. This is a defensive fallback but not the clean solution.

**Recommended: Approach 1.** The schedule should store the actual prompt text, not the config JSON. This means:
- The admin UI's gallivanting schedule creator should extract `prompt_template` and store it as `message`
- The other fields (`duration_minutes`, `random_offset_minutes`) are schedule-level config and should be stored as schedule metadata or dedicated columns
- If no `prompt_template` is provided, fall back to the built-in template at `templates/gallivanting/en.md`

## Context

- Discovered during gallivanting session debug (2026-08-03)
- The agent still performed well (creative output was strong) but the raw JSON in the prompt is messy and wastes context tokens
- Related to the broader question of how gallivanting schedules are configured in the admin UI
# Ticket 037 — Study Tool

**Priority:** P2 (builds on diary; first structured self-directed activity)
**Status:** Open
**Dependencies:** Ticket 029 (Multi-tenant — per-agent scoping), Ticket 036 (Diary — study reports may optionally be recorded in diary)
**Updated:** 2026-05-06

## Summary

A structured reading and analysis tool for processing stored works segment-by-segment. Materials are prepared in an agent-reader-compatible format (e.g., splitting a book into chapters), then the study tool enables sequential reading, per-segment reporting, and complete-work synthesis.

## Motivation

Agents need a mechanism for deep, structured engagement with source material — not just query-answer but genuine sequential absorption. Current agent-reader (external tool) handles text segmentation but not the reading/reporting cycle. The study tool internalizes that cycle as a first-class Animus capability.

Use cases:
- **Gallivanting integration:** Study is presented as an option during gallivanting blocks — structured self-directed time
- **Professional literature:** In settings where staying current on literature matters, study becomes a scheduled professional development function
- **General knowledge building:** Any stored work (books, papers, documentation) can be absorbed into the agent's memory with structured analysis

## Architectural Model

```
Source material → segment → store → study (read + report)
```

The pipeline is: prepare material, store segments, agent reads segments in order, writes positionally-aware reports, and produces a synthesis for the complete work. Memory consolidation is a separate passive process that scans all agent actions — study doesn't need to know about it.

## Data Model

### study_materials table

| Column | Type | Description |
|--------|------|-------------|
| id | TEXT (UUID) | Primary key |
| agent_id | TEXT (UUID) | Owner agent (multi-tenant scoping) |
| title | TEXT | Human-readable title |
| source | TEXT | Origin (URL, file path, manual) |
| total_segments | INTEGER | Number of segments |
| segment_format | TEXT | "text", "markdown", "html_stripped" |
| status | TEXT | "pending", "in_progress", "complete", "abandoned" |
| created_at | INTEGER | Unix ms timestamp |
| updated_at | INTEGER | Unix ms timestamp |

### study_segments table

| Column | Type | Description |
|--------|------|-------------|
| id | TEXT (UUID) | Primary key |
| material_id | TEXT (UUID) | FK → study_materials |
| segment_index | INTEGER | 0-based position |
| content | TEXT | Segment content |
| checksum | TEXT | Content hash for change detection |

### study_reports table

| Column | Type | Description |
|--------|------|-------------|
| id | TEXT (UUID) | Primary key |
| material_id | TEXT (UUID) | FK → study_materials |
| agent_id | TEXT (UUID) | Owner agent |
| segment_index | INTEGER | NULL for complete-work synthesis |
| content | TEXT | Report content |
| created_at | INTEGER | Unix ms timestamp |

## Tool Interface

```json
{
  "name": "study",
  "operations": [
    "list — show available materials with progress",
    "load — register new material (upload segments or reference external source)",
    "read — get next unread segment (or specific segment index)",
    "report — write per-segment report (positionally aware)",
    "synthesize — write complete-work synthesis report",
    "progress — show current position and completion status",
    "abandon — mark material as abandoned with optional reason"
  ]
}
```

### Positional awareness

Reports for segment N have access to:
- The content of segment N
- All prior reports for segments 0 through N-1
- The report template (summary, key claims, connections, open questions)

This ensures chapter 5 knows what chapters 1–4 established. Each report builds on the cumulative understanding.

### Report template (default)

```json
{
  "summary": "2-3 sentence overview",
  "key_claims": ["list of assertions made"],
  "connections": ["links to prior segments, external knowledge, or the agent's experience"],
  "open_questions": ["what this segment raises but doesn't resolve"]
}
```

Templates are configurable per material.

## Admin API

```
GET    /api/v1/agents/{id}/study/materials          → list materials + progress
GET    /api/v1/agents/{id}/study/materials/{mid}      → material detail + segment progress
GET    /api/v1/agents/{id}/study/materials/{mid}/reports → all reports for material
POST   /api/v1/agents/{id}/study/materials            → upload new material
DELETE /api/v1/agents/{id}/study/materials/{mid}      → abandon material
```

Admin can see what materials exist and progress status, but not agent report content (same privacy model as diary: metadata visible, content agent-private).

## Integration Points

- **Diary (036):** Study reports may optionally be recorded in diary as reflection entries (agent's choice, not automatic)
- **Memory:** No direct integration. Memory consolidation is a passive background process that scans all agent actions; study activity is picked up automatically like everything else.
- **Gallivanting (038):** Study offered as structured option during gallivanting blocks
- **Scheduler (024):** Study blocks can be scheduled (e.g., "study 2 segments per day")

## Acceptance Criteria

- [ ] `StudyManager` class owns material/segment/report lifecycle
- [ ] Per-agent study scoping follows multi-tenant pattern
- [ ] Study tool with all 7 operations (list, load, read, report, synthesize, progress, abandon)
- [ ] Positional awareness: segment N reports receive all prior reports as context
- [ ] Admin API for material management (metadata only for reports)
- [ ] Configurable report templates
- [ ] Resume-from-last-read semantics per material per agent
- [ ] All existing tests continue to pass
- [ ] New study tests cover: material creation, segment reading, report writing, positional context, synthesis

## Key Questions

- Segment format: plain text only for v1, or support markdown?
- Report schema: freeform or structured template only?
- Who prepares materials: manual upload only, or tool-assisted splitting?
- Memory consolidation: not study's concern — passive background process handles it
- Storage: separate SQLite tables in existing store, or separate database?

## Risks

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| Large materials (full books) consume storage | Medium | Segments stored compressed; materials can be abandoned |
| Report quality varies with segment granularity | Medium | Configurable segment size; agents can request re-segmentation |
| Positional context window grows with segment count | Medium | Summarize prior reports after N segments to keep context bounded |
| Privacy model confusion (study vs. diary) | Low | Study reports are agent-private but material metadata is admin-visible; document clearly |
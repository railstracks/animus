# Ticket 011 — Memory Browser UI

**Priority:** P2
**Status:** Open
**Dependencies:** Ticket 004 (Memory Layer API), Ticket 009 (SPA scaffold)

## Summary

Build the memory layer browser interface within the SPA. Layer tree, observation list, detail panel, consolidation controls.

## Scope

- Layer tree in sidebar: collapsible, shows observation count per layer, visual indicator of layer "weight" (e.g., color density)
- Observation list (main area): sortable by weight, age, tags; paginated; filterable by tags
- Observation detail panel (right drawer or modal): full content, metadata (tags, weight, decay_rate, timestamps, layer)
- Consolidation controls: trigger button, progress indicator, last run summary (promoted/demoted/archived counts)
- Observation editing: modify tags, weight via inline controls
- Visual decay visualization (e.g., progress bar showing decay status)

## Acceptance Criteria

- [ ] Layer tree reflects configured layers from the API
- [ ] Observation list loads and paginates correctly
- [ ] Clicking an observation shows full detail
- [ ] Consolidation trigger works and shows progress
- [ ] Tag and weight edits persist via API

## Notes

- This is a power-user feature; clarity over flashiness
- Consider a "compact" vs "detailed" view toggle for the observation list

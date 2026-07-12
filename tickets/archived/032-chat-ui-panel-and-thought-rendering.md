# Ticket 032 — Chat UI Right Panel Tabs + Thought Rendering Polish

**Priority:** P1 (high UX impact, improves session navigation and response clarity)
**Status:** Open
**Dependencies:** Ticket 010 (Chat UI), Ticket 031 (Agent Form Provider/Model Refinements)
**Updated:** 2026-05-04

## Summary

Revise the Chat UI right-side panel into a tabbed interface and improve rendering of leaked/internal thought channel markers.

This ticket introduces:

1. A right-panel tabbed layout with:
   - Context/Settings tab (current behavior)
   - Sessions tab (full session list and quick switch UX)
2. Thought channel rendering polish:
   - Keep thought content visible for clarity.
   - Detect and normalize channel marker artifacts (for example escaped `\u003c|channel\u003ethought` segments).
   - Render thought content in a distinct style (italicized, color-separated from main assistant content).

## Motivation

Current behavior has two gaps:

1. Session navigation is split from context controls, and a dedicated sessions pane in the right rail will improve workflow when hopping across conversations.
2. Some model outputs include raw channel marker artifacts (`<|channel|>thought`, escaped variants), which currently display as noisy inline text. This is valuable context, but needs structured presentation.

## Scope

### In Scope

1. Right panel `v-tabs` with at least:
   - `Context` tab: current context/settings cards.
   - `Sessions` tab: complete session list with metadata and select action.
2. Sessions tab behavior:
   - Shows all sessions returned by session API/websocket list.
   - Highlights currently selected session.
   - Supports click-to-switch to selected session.
3. Thought marker normalization and rendering:
   - Detect literal and escaped channel markers.
   - Extract thought segments into the existing thought UI surface.
   - Keep thought content separate from assistant main body.
   - Render thought style as italic with distinct color treatment.
4. Preserve existing message streaming behavior and session binding rules.

### Out of Scope

1. Changing core chat protocol/event schemas (`token`, `thinking`, `text`, `done`).
2. Reworking model/provider response parsing outside thought-marker cleanup.
3. Major redesign of top toolbar controls (agent/session selectors stay as currently scoped).

## Design Plan

### Phase 1: Right Panel Tabs

1. Refactor right panel in `ChatView.vue` to use tabs:
   - Tab A: Context
   - Tab B: Sessions
2. Move existing context cards unchanged into the Context tab initially.
3. Implement Sessions tab list with:
   - session id
   - source
   - message count
   - last active timestamp
4. Wire session-row click to existing `switchSession(...)` flow.

### Phase 2: Thought Marker Sanitizer/Normalizer

1. Add a lightweight normalization layer for assistant text chunks that may contain channel markers:
   - `<|channel|>thought ... <channel|>`
   - escaped variants such as `\u003c|channel\u003ethought ... \u003cchannel|\u003e`
2. Parse and route extracted thought text into `message.thinkingContent`.
3. Remove marker artifacts from `message.content` while preserving non-thought text.
4. Apply the same logic to:
   - live streaming token assembly
   - full text events
   - history hydration path (optional best effort where fields are already persisted mixed)

### Phase 3: Visual Treatment

1. Style thought block to be clearly differentiated:
   - italicized body
   - softer contrasting color
   - maintain collapsible interaction
2. Ensure readability across desktop/mobile and existing theme.

### Phase 4: Regression and Safety

1. Verify no regressions in:
   - websocket streaming completion
   - stop generation flow
   - session switching
   - agent selection for new sessions
2. Add focused tests where practical (unit/helper-level for thought parser; UI sanity checks if test harness exists).

## Concrete File Targets

- `admin-ui/src/views/ChatView.vue`
- `admin-ui/src/i18n/locales/en.ts` (tab labels and helper text)
- `admin-ui/src/i18n/locales/nl.ts` (tab labels and helper text)
- Optional helper extraction if needed:
  - `admin-ui/src/lib/chatFormatting.ts` (thought marker parsing/normalization)

## Acceptance Criteria

- [ ] Right panel uses tabs with Context and Sessions panes.
- [ ] Sessions tab shows complete session list and supports switch-to-session action.
- [ ] Channel marker artifacts no longer appear raw in assistant body text.
- [ ] Thought segments are preserved and rendered in dedicated thought styling.
- [ ] Thought text is italicized and visually distinct from main assistant response.
- [ ] Chat streaming/session behavior remains stable.
- [ ] UI build passes.

## Risks and Mitigations

1. **Risk:** Over-aggressive thought parsing may strip legitimate user-visible content.
   - **Mitigation:** Restrict parser to explicit marker patterns only and keep fallback to original text when parsing is ambiguous.
2. **Risk:** Streaming chunk boundaries split marker sequences.
   - **Mitigation:** Parse on accumulated buffer/message text, not per-token in isolation.
3. **Risk:** Sessions tab duplicates or diverges from existing session selector behavior.
   - **Mitigation:** Reuse shared session state (`sessions`, `selectedSession`, `switchSession`) as single source of truth.

## Rollout Notes

1. Land tabbed panel refactor first (no behavior changes).
2. Land thought normalization/rendering second.
3. Validate with manual streaming scenarios containing thought markers.
4. Add `tickets/032-chat-ui-panel-and-thought-rendering.report.md` after completion.


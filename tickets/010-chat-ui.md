# Ticket 010 — Chat UI

**Priority:** P1
**Status:** Open
**Dependencies:** Ticket 005 (WebSocket chat endpoint), Ticket 009 (SPA scaffold)

## Summary

Build the conversational chat interface within the SPA. Real-time streaming responses, session management, and context display.

## Scope

- Chat view with message input area (bottom-fixed, expandable textarea)
- Message stream display (user messages right-aligned, agent messages left-aligned)
- Streaming token rendering: tokens appear as they arrive (typewriter effect or append-per-chunk)
- Session selector: switch between active sessions or create new
- Session history: load and display past messages when switching sessions
- Context sidebar panel: shows which memory layers are active, loaded observations, available tools
- Markdown rendering for agent responses (code blocks, lists, links)
- Auto-scroll to latest message (with manual scroll-up to pause)
- "Stop generating" button to interrupt in-progress responses

## Acceptance Criteria

- [ ] Messages send via WebSocket and stream back token-by-token
- [ ] Session switching loads history without losing current session state
- [ ] Context panel updates when session changes
- [ ] Markdown renders correctly (at minimum: code blocks, bold, links)
- [ ] Stop button cancels generation and preserves partial response
- [ ] Empty state is helpful ("Start a conversation..." prompt)

## Notes

- Consider message size: long responses should render incrementally, not buffer entire response
- Session history pagination for very long conversations

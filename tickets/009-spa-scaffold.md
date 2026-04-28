# Ticket 009 — SPA Scaffold (Vue 3 + Vuetify)

**Priority:** P1
**Status:** Open
**Dependencies:** Tickets 002, 003, 005 (API endpoints should exist before UI consumes them)

## Summary

Set up the Vue 3 + Vuetify single-page application that serves as the admin UI. Establish the project structure, routing, layout, and build pipeline.

## Scope

- Vue 3 project with Vite build tooling
- Vuetify 3 component library
- Vue Router with sidebar navigation
- Layout: sidebar + main content area
- Pages/routes:
  - `/` — Dashboard (agent status, health, recent activity summary)
  - `/chat` — Chat interface (Ticket 010)
  - `/memory` — Memory browser (Ticket 011)
  - `/config` — Agent configuration (model, system prompt, settings)
  - `/interfaces` — Interface management
  - `/constitution` — Constitution viewer
  - `/logs` — Log viewer (future)
- API client layer (axios or fetch wrapper, configured with base URL)
- WebSocket client utility for chat and observation streams
- Basic authentication support (token in header/query)
- Dark theme default (cozy, not corporate)

## Acceptance Criteria

- [ ] `npm run build` produces static assets in `dist/`
- [ ] All routes render with placeholder content
- [ ] API client connects to configurable backend URL (default localhost:8080)
- [ ] Sidebar navigation works with active-state highlighting
- [ ] Responsive layout (usable on desktop; mobile is secondary)

## Notes

- This should live in a `ui/` or `admin-ui/` directory within the Animus repo
- Build output will be consumed by Ticket 012 (resource embedding)
- Consider using Vuetify presets for consistent theming

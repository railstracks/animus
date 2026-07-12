# Ticket 104 — Dashboard Redesign

## Problem

The current dashboard (`/`) shows two placeholder cards: "System Status" (raw uptime number) and "Recent Activity" (literal placeholder text referencing tickets 010/011 from May). For a v1 release, this is the first impression new users get — it needs to answer "is everything okay?" in under 5 seconds.

## Goal

Rebuild the dashboard as an operational triage surface. An operator landing on `/` should immediately see daemon health, recent activity, provider status, and upcoming work — without navigating deeper.

## Design

### Layout

```
┌─────────────────────────────────────────────────────────┐
│  ● Running · Uptime 10h 52m · 2 agents · 4 sessions    │  ← Status ribbon
├──────────────┬──────────────┬───────────────────────────┤
│  Recent      │  Memory      │  Providers                │
│  Sessions    │  Health      │                           │
│              │              │                           │
├──────────────┴──────────────┴───────────────────────────┤
│  Scheduled Jobs         │  Quick Actions                │
│                         │                               │
└─────────────────────────┴───────────────────────────────┘
```

### Row 1: Status Ribbon

Single-line horizontal bar, no card chrome. Compact, scannable.

- **State indicator:** colored dot (green = ok, yellow = degraded, red = error)
- **Uptime:** humanized (e.g. "10h 52m", not raw seconds)
- **Agent count:** `N agents` (click-through to /agents)
- **Active session count:** `N active sessions` (click-through to /chat)
- **Version label:** from build or API

Source: `GET /api/v1/status` (already returns status + uptime), `GET /api/v1/agents` (agent count), `GET /api/v1/sessions?status=active` (session count).

### Row 2: Three Cards

#### Card A: Recent Sessions

Last 5-8 sessions, compact list:

- Agent name (bold) · Channel badge
- Last message preview (1 line, truncated ~60 chars)
- Relative timestamp ("3m ago", "2h ago")

Click-through to `/chat` with that session. Empty state: "No sessions yet — start a chat."

Source: `GET /api/v1/sessions?status=recent&limit=8`

#### Card B: Memory Health

Compact overview:

- **Last consolidation:** relative time (e.g. "12m ago") or warning ("Never run" / "Overdue")
- **Pending observations:** count with subtle highlight if > threshold
- **Layer summary:** small inline bars or numbers: `Day: 42 · Week: 18 · Month: 7`
- **Embedding status:** indicator (enabled/disabled)

Empty state: "Memory system idle. Consolidation runs automatically."

Source: `GET /api/v1/memory/inspect` (layer stats), consolidation last-run timestamp from scheduler or system state.

#### Card C: Provider Status

List of configured providers, one row each:

- Provider ID (bold) · Provider type badge
- Reachability dot (green = responded to last request, red = error/timeout, gray = untested)
- Model in use
- Last error message (if any, truncated)

Empty state: "No providers configured. Visit the Providers page or run the setup wizard."

Source: `GET /api/v1/providers`

### Row 3: Two Cards

#### Card D: Upcoming Scheduled Jobs

Next 3-5 scheduled jobs:

- Job name/tag
- Next fire time (relative: "in 2h", "in 3d")
- Brief description (truncated)

Click-through to `/scheduler`. Empty state: "No scheduled jobs."

Source: `GET /api/v1/scheduler/jobs` (filter by upcoming).

#### Card E: Quick Actions

Button grid:

- **New Chat** → `/chat`
- **Add Agent** → `/agents` (or `/wizard` if no agents)
- **Configure Provider** → `/providers`
- **View Logs** → `/logs`
- **Run Setup Wizard** → `/wizard` (only if no agents)

Small, icon-labeled buttons. Not full-width — compact grid.

No API calls needed — pure router links.

### Responsive Behavior

- **Desktop (≥961px):** Row 2 = 3 columns. Row 3 = 2 columns.
- **Tablet (601-960px):** Row 2 = 2 columns (Recent Sessions full width on top, Memory + Providers side by side). Row 3 = 1 column.
- **Mobile (≤600px):** Everything stacks vertically.

### Visual Style

- Cards: existing `rounded="xl"` + subtle elevation
- Status ribbon: flat, no card, uses surface tint
- Colored indicators: green (#4CAF50), yellow (#FF9800), red (#F44336) — consistent with existing alert colors
- Monospace for numbers/metrics (uptime, counts)
- No charts in v1 — numbers and text only. Charts add complexity and we don't have a charting library yet.

## Technical Implementation

### Frontend only

No backend changes. All data sources are existing API endpoints. The dashboard makes 4-5 parallel API calls on mount:

```typescript
const [status, agents, sessions, providers, memory] = await Promise.allSettled([
  apiGet('/api/v1/status'),
  apiGet('/api/v1/agents'),
  apiGet('/api/v1/sessions?status=recent&limit=8'),
  apiGet('/api/v1/providers'),
  apiGet('/api/v1/memory/inspect'),
]);
```

`Promise.allSettled` so one failing endpoint doesn't break the whole page — each card handles its own error state independently.

### Files changed

- `admin-ui/src/views/DashboardView.vue` — full rewrite
- `admin-ui/src/i18n/locales/en/dashboard.ts` — new labels

### Helper functions

```typescript
function formatUptime(seconds: number): string {
  // 39292 → "10h 52m"
}

function relativeTime(timestamp: string): string {
  // ISO → "3m ago", "2h ago", "5d ago"
}
```

These are reusable — put in `admin-ui/src/lib/format.ts` for use by other views.

### Error handling per card

Each card independently handles:
- Loading state (skeleton or "Loading...")
- Error state ("Failed to load" with retry button)
- Empty state (specific guidance per card)
- Populated state

### First-run banner integration

The dismissible first-run banner (from ticket 100) stays — it renders above the dashboard content when no agents exist.

## Scope

### In scope:
- Status ribbon with daemon/agent/session counts
- Recent sessions card
- Memory health card
- Provider status card
- Upcoming jobs card
- Quick actions card
- Responsive layout
- Per-card loading/error/empty states
- Reusable formatting helpers

### Deferred:
- Real-time updates (WebSocket/polling) — static load on mount for v1
- Charts/sparklines — needs charting library
- Token usage/cost tracking — needs backend data pipeline
- Customizable widget layout — user-draggable cards
- Dark/light mode toggle per-widget

## Priority

Medium — important for first impressions but not a release blocker. The dashboard works (shows status), it's just embarrassing.

## Estimated Complexity

Small-medium. Single file rewrite + formatting helpers. ~300-400 lines of Vue. No backend work.

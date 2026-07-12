# Ticket 103 — Split i18n Locale Files by Section

## Problem

All 23 locale files (647 lines in `en.ts`) are single monolithic files. Each top-level section (`app`, `sidebar`, `chat`, `memory`, `channels`, etc.) is crammed into one file. This causes:

- **Maintenance pain** — finding a specific label requires scrolling through 600+ lines
- **Translation friction** — translators must work with one massive file per language, making batch translation harder and merge conflicts likely
- **Cognitive overhead** — the file size discourages adding new labels, since every language file must be updated in lockstep
- **Inconsistent completeness** — English is 647 lines, most translations are ~422 (65%), making it hard to identify which sections are missing

## Goal

Split each locale file from a single `<lang>.ts` into a `<lang>/` directory containing one file per top-level section, with an `index.ts` that assembles them.

## Design

### Current Structure

```
admin-ui/src/i18n/locales/
  en.ts          ← 647 lines, 16 sections
  nl.ts          ← 546 lines
  zh.ts          ← 422 lines
  ... (23 languages)
```

### Proposed Structure

```
admin-ui/src/i18n/locales/
  en/
    index.ts     ← assembles all sections, exports `en`
    app.ts
    sidebar.ts
    common.ts
    dashboard.ts
    chat.ts
    memory.ts
    memorySearch.ts
    memoryFiles.ts
    ontology.ts
    providers.ts
    config.ts
    constitution.ts
    logs.ts
    agents.ts
    activeMemory.ts
    channels.ts
  nl/
    index.ts
    app.ts
    ... (same section files)
  ... (same for all languages)
```

### Index File Pattern

```typescript
// en/index.ts
import { app } from './app'
import { sidebar } from './sidebar'
import { common } from './common'
import { dashboard } from './dashboard'
import { chat } from './chat'
import { memory } from './memory'
import { memorySearch } from './memorySearch'
import { memoryFiles } from './memoryFiles'
import { ontology } from './ontology'
import { providers } from './providers'
import { config } from './config'
import { constitution } from './constitution'
import { logs } from './logs'
import { agents } from './agents'
import { activeMemory } from './activeMemory'
import { channels } from './channels'

export const en = {
  app,
  sidebar,
  common,
  dashboard,
  chat,
  memory,
  memorySearch,
  memoryFiles,
  ontology,
  providers,
  config,
  constitution,
  logs,
  agents,
  activeMemory,
  channels,
} as const
```

### Section File Pattern

Each section file exports its slice of the tree:

```typescript
// en/chat.ts
export const chat = {
  sessionLabel: 'Session',
  newSession: 'New Session',
  refresh: 'Refresh',
  // ...
} as const
```

### Import Changes

The consumer (`admin-ui/src/i18n/index.ts`) changes its imports:

```typescript
// Before:
import { en } from './locales/en'

// After:
import { en } from './locales/en/index'
```

Or with path aliases:
```typescript
import { en } from './locales/en'
// Works with both file (en.ts) and directory (en/index.ts)
// TypeScript resolves directory imports automatically
```

**TypeScript resolves `./locales/en` to `./locales/en/index.ts` when `en.ts` doesn't exist.** This means no import changes are needed — the consuming code keeps importing from `'./locales/en'` and it just works.

### Identifying the 16 Sections

From `en.ts`, the top-level keys are:

| Section | Lines | Complexity |
|---------|-------|------------|
| `app` | 10 | Low |
| `sidebar` | 24 | Low |
| `common` | 5 | Low |
| `dashboard` | 19 | Low |
| `chat` | 69 | Medium |
| `memory` | 81 | Medium |
| `memorySearch` | 19 | Low |
| `memoryFiles` | 105 | High |
| `ontology` | 23 | Low |
| `providers` | 67 | Medium |
| `config` | 4 | Low |
| `constitution` | 4 | Low |
| `logs` | 4 | Low |
| `agents` | 84 | High |
| `activeMemory` | 22 | Low |
| `channels` | 105 | High |

## Implementation Plan

### Step 1: Script the split

Write a Node.js script (`scripts/split-i18n.ts`) that:
1. Reads each `locales/<lang>.ts` file
2. Parses the top-level object literal (regex or AST)
3. Creates `locales/<lang>/` directory
4. Writes one file per top-level key
5. Writes `index.ts` that imports and re-exports all sections
6. Deletes the original `<lang>.ts` file

This avoids manual errors across 23 files × 16 sections = 368 files.

### Step 2: Verify TypeScript resolution

- Ensure `vue-tsc` type checking passes
- Ensure dev server (`npm run dev`) loads translations correctly
- Ensure production build (`npm run build`) succeeds

### Step 3: Identify missing translations

After the split, add a utility script (`scripts/i18n-audit.ts`) that:
- Compares each language's section files against English
- Reports which sections are missing or incomplete per language
- Outputs a summary table

This gives translators a clear per-section worklist.

## Scope

### In scope:
- Split all 23 locale files into per-section directories
- Index files that assemble sections
- Verification that TypeScript path resolution works unchanged
- Audit script for translation completeness

### Deferred:
- Migration to JSON format (if ever needed for non-TS translation tools)
- Automated translation pipeline
- Per-section hot-reload (Vite already handles this via HMR on individual files)

## Priority

Low-medium — code quality improvement. No functional impact. Worth doing before adding more labels or languages.

## Estimated Complexity

Small. Mostly mechanical splitting. The script does the heavy lifting; verification is straightforward.

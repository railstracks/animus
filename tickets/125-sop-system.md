# Ticket 125: SOP System

**Status:** Scoped
**Date:** 2026-07-20
**Author:** Melvin + Kestrel

## Problem

Agents face recurring task patterns — setting up roleplay games, organizing software projects, configuring channels, running migrations — but have no access to structured procedures for these workflows. Expertise lives in documentation that agents never see, or gets crammed into system prompts where it burns tokens regardless of relevance.

We need a pull-based model: agents discover procedures on demand, load the ones relevant to the current task, and internalize them into persistent memory.

## Concept

**SOPs (Standard Operating Procedures)** are markdown files that encode reusable workflows, setup guides, best practices, and task templates. They live in the Animus repository and are available to agents via a tool.

The key differentiator from RAG retrieval: SOPs don't just return text into the context window. They are **written to MemoryFiles**, which means:
1. The content persists across sessions (not ephemeral context)
2. The memory consolidation pipeline processes them (principles extracted, not just steps)
3. The agent internalizes the procedure over time, rather than re-reading it each session

## Design

### Tool: `sop`

Single tool with multiple actions. The agent can browse available SOPs, search by keyword or category, and load relevant ones into memory.

**Tool definition:**
```
name: "sop"
description: "Browse and load Standard Operating Procedures (SOPs) — reusable 
             workflow guides, setup procedures, and best-practice templates. 
             Loading an SOP writes it to memory for persistent access and 
             consolidation. Use 'list' to see available SOPs, 'search' to find 
             by keyword, or 'load' to internalize a specific SOP."

actions:
  list:
    parameters:
      category: string (optional) — filter by category
      page: integer (optional, default 1) — pagination
      per_page: integer (optional, default 20) — items per page
    returns: paginated list of SOPs with name, category, description, version

  search:
    parameters:
      query: string (required) — search keyword/phrase
      page: integer (optional, default 1)
      per_page: integer (optional, default 20)
    returns: paginated matching SOPs ranked by relevance

  load:
    parameters:
      name: string (required) — SOP identifier (slug)
    returns: success confirmation, MemoryFile path
    side effects: writes SOP content to MemoryFiles, stages for consolidation
```

### SOP File Format

Markdown files with YAML frontmatter:

```markdown
---
name: setup-vtm-roleplay
title: Setting Up a Vampire: The Masquerade Roleplay Game
category: roleplay
tags: [vtm, roleplay, storytelling, world-of-darkness]
version: 1.0.0
description: >
  Complete procedure for initializing a VtM campaign — character creation,
  scene setup, combat resolution, and storyteller principles.
---

# Setting Up a VtM Roleplay Game

## Prerequisites
...

## Character Creation
...

## Scene Setup
...
```

**Fields:**
- `name` — unique slug, used as identifier
- `title` — human-readable display name
- `category` — top-level grouping (e.g., `roleplay`, `development`, `devops`, `configuration`)
- `tags` — searchable keywords
- `version` — semver for tracking updates
- `description` — one-line summary for list/search results

### Storage

**Phase 1: Local filesystem.**

SOPs live in `dataDir/sops/` as `.md` files. Bundled with the Animus distribution. Admin can add or update SOPs by dropping files into the directory.

No runtime network dependency. SOPs update on version bumps or manual `git pull`.

**Future consideration:** configurable remote source (e.g., an org's own SOP repository) — but not Phase 1.

### MemoryFile Integration

When `load` is called:
1. Read the SOP markdown file from `dataDir/sops/{name}.md`
2. Write to MemoryFiles as a new file (e.g., `memoryFile.sop.{name}.md`)
3. The MemoryFile system handles inclusion in prompt context (loaded as memory layer)
4. The consolidation pipeline picks it up during the next intake cycle

This means the SOP content becomes part of the agent's persistent memory — not just a one-time context injection. The consolidation system can extract principles, cross-reference with existing memory, and build understanding over time.

### SOP Store

`SopStore` reads the `dataDir/sops/` directory and caches the catalog in memory:
- On startup: scan directory, parse frontmatter, build catalog
- `List(category, page, perPage)` — paginated listing
- `Search(query, page, perPage)` — keyword search across name, title, tags, description
- `Get(name)` — full content of a specific SOP
- File watcher (optional) — reload catalog if directory changes

### Browsing UX in Admin UI

Optional for Phase 1, but a simple SOP browser page in the admin UI would be valuable:
- Grid of SOP cards grouped by category
- Search bar
- Click to read full SOP content
- Shows which agents have loaded each SOP (via MemoryFiles)

Defer to Phase 2 if scope is too large.

## Implementation Plan

### Files to Create
- `include/animus_kernel/tools/SopTool.h`
- `src/kernel/tools/SopTool.cpp`
- `include/animus_kernel/SopStore.h`
- `src/kernel/sop/SopStore.cpp`
- `sops/` directory with initial SOP examples (2-3 starter SOPs)

### Files to Modify
- `include/animus_kernel/AgentKernel.h` — add `SopStore` member
- `src/kernel/AgentKernel.cpp` — create store, register tool
- `CMakeLists.txt` — new sources

### Initial SOP Library (Starters)

Include 2-3 example SOPs to validate the system:

1. **`setup-roleplay-game`** — initializing a roleplay campaign (character creation, scene templates, tone guidelines)
2. **`organize-software-project`** — project structure conventions, ticket workflow, branching strategy
3. **`configure-channel`** — channel setup procedure (connector config, auth, testing)

These serve as both useful content and format examples for contributors.

## Phases

**Phase 1 (this ticket):**
- `SopStore` — directory scan, frontmatter parsing, catalog cache
- `sop` tool with `list`, `search`, `load` actions
- MemoryFile integration (load writes to memory)
- 2-3 starter SOPs
- CMakeLists + AgentKernel wiring

**Phase 2:**
- Admin UI SOP browser page
- Remote SOP source (configurable repository)
- Version tracking (diff old vs new when SOP updated)
- Usage analytics (which SOPs are loaded most)

## Open Questions

1. **Should loaded SOPs expire?** — If an agent loads an SOP, should it eventually age out of memory, or persist indefinitely? My lean: persist. The consolidation system will naturally reduce prominence if unused. But worth deciding explicitly.

2. **Per-agent or per-session SOPs?** — Should SOPs load into agent-level memory (persisted across all sessions for that agent) or session-level (only for the current session)? My lean: agent-level. That's where MemoryFiles live.

3. **Access control?** — Should some SOPs be restricted to certain agent types? Probably not for Phase 1, but worth noting if SOPs with sensitive procedures are added later.

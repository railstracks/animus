---
name: organize-software-project
title: Organizing a Software Project
category: development
tags: [project-structure, tickets, git, workflow]
version: 1.0.0
description: >
  Conventions for structuring a software project — repository layout,
  ticket workflow, branching strategy, and documentation standards.
---

# Organizing a Software Project

## Repository Layout

```
project-name/
├── src/              # Source code
├── include/          # Header files (C/C++)
├── tests/            # Test suite
├── docs/             # Documentation
├── scripts/          # Build/deploy scripts
├── tickets/          # Ticket definitions and reports
├── CMakeLists.txt    # or package.json, Gemfile, etc.
├── README.md
├── LICENSE
└── CHANGELOG.md
```

## Ticket Workflow

1. **Scope** — Write a ticket file defining the problem, design, and acceptance criteria
2. **Implement** — Create a branch (`ticket/NNN-description`), implement the feature
3. **Test** — Verify against acceptance criteria
4. **Report** — Write a `NNN-report.md` summarizing what was built, decisions made, and issues encountered
5. **Review** — Code review, merge to main branch
6. **Tag** — Version bump if releasing

## Branching Strategy

- `main` — stable, always builds
- `dev` — integration branch for features
- `ticket/NNN-*` — individual ticket work
- `hotfix/*` — urgent production fixes

## Documentation Standards

- Every project has a README explaining what it is and how to start
- Tickets document the "why" — not just the "what"
- Report files capture decisions and rationale for future reference
- CHANGELOG.md tracks user-facing changes per version

## Commit Messages

Format: `type: brief description`

Types: `feat`, `fix`, `docs`, `chore`, `refactor`, `test`, `perf`

Include the "why" in the body when it's not obvious from the title.

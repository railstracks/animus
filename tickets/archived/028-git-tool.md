# Ticket 028 — Git Tool

**Priority:** P2 (deferred from Ticket 022 — too large for the original ticket)
**Status:** Open
**Dependencies:** Ticket 021 (Tool Calling and Reasoning Mode), Ticket 022 (file/shell tools — done)
**Updated:** 2026-05-03

## Summary

Implement a structured Git tool as a first-class interface to version control operations. Deferred from Ticket 022 because it constitutes a major operation — too large to squeeze alongside file read/write and shell_exec.

## Motivation

Git is the primary version control system and a critical tool for any agent working with code. While `shell_exec` can run `git` commands, a structured tool provides:
- Type-safe parameters (no shell injection risk)
- Consistent output parsing
- Working directory context management
- Audit trail of version control operations

## Tool Definition

*To be specified. Git operations to cover:*

- `status` — working tree status
- `log` — commit history
- `diff` — changes between commits/working tree
- `add` / `reset` — staging operations
- `commit` — create commits
- `branch` — branch listing/creation
- `checkout` / `switch` — branch switching
- `merge` — branch merging
- `pull` / `push` / `fetch` — remote operations
- `stash` — temporary storage

## Notes

- Git was deferred from Ticket 022 specifically because it's a major operation. The tool surface is large (10+ subcommands) and each needs careful parameter definition and output parsing.
- Working directory context: the tool needs to know which repository it's operating in. This may depend on session context or explicit `repo_path` parameter.
- Authentication: remote operations (push, pull, fetch) need credential handling — SSH keys, tokens, etc.
- This is structured sugar over shell execution, not a libgit2 integration. Keep it simple.
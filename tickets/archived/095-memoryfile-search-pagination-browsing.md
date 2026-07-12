# Ticket 095 — MemoryFile Search Pagination and Browsing

**Status:** Open  
**Priority:** Medium  
**Created:** 2026-06-16  
**Depends on:** None  
**Component:** Memory System / Tools

## Summary

Extend the `memory` tool's MemoryFile handling with three new access modes: `lines` (range-based content retrieval), `regex` (pattern-based search), and `browse` (paginated file listing). These allow agents to navigate large MemoryFiles without needing to fetch entire files at once.

## Motivation

Currently, MemoryFile content is accessible through:
- `memory_search` — vector search returning truncated snippets
- `memory_get` — reads a file with optional `from`/`lines` parameters for line-based pagination

This works for targeted retrieval but doesn't support:
1. **Structured browsing.** An agent wanting to survey all available MemoryFiles has no way to page through them casually. They'd need to know specific file names or search terms.
2. **Regex-based search.** Looking for all references to a pattern (e.g., every date, every proper noun, every ticket number) requires reading entire files.
3. **Efficient line-range access.** The existing `memory_get` supports `from` and `lines`, but this is not integrated with search results — an agent can't say "show me lines 50-100 of the file that matched my search."

As MemoryFiles grow (especially during migration, where `memory_expanded/` files can be thousands of lines), agents need structured browsing rather than all-or-nothing retrieval.

## Design

### `memory_search` — new parameters

Add `regex` parameter to `memory_search`:

```ruby
# Existing: vector search by query
memory_search(query: "Nagel", corpus: "memory")

# New: regex search across MemoryFiles
memory_search(regex: "Ticket \d{3}", corpus: "memory")
# Returns matching lines with file path and line numbers
```

Regex search:
- Searches across all `active` MemoryFiles.
- Returns matching lines with `source_path`, `startLine`, `endLine`, and `snippet`.
- Respects `maxResults` parameter (default 20).
- Does NOT perform vector ranking — results are ordered by file path then line number.

### `memory_get` — new parameters

Add `lines` range support and `browse` mode to `memory_get`:

```ruby
# Existing: read full file or from offset
memory_get(path: "philosophy.md", from: 50, lines: 30)

# New: read a specific line range
memory_get(path: "philosophy.md", lines: "50-80")
# Equivalent to from=50, lines=30 but more intuitive for agents

# New: regex within a specific file
memory_get(path: "philosophy.md", regex: "Nagel")
# Returns all lines in this file matching the pattern, with line numbers
```

### `memory_browse` — new action

Add a `browse` action for paginated MemoryFile listing:

```ruby
# List all MemoryFiles, paginated
memory_get(action: "browse", page: 1, results_per_page: 10)

# Returns:
# {
#   total_files: 18,
#   total_pages: 2,
#   current_page: 1,
#   results_per_page: 10,
#   files: [
#     { path: "philosophy.md", status: "active", size_bytes: 4521, lines: 142, last_updated: "2026-06-15" },
#     { path: "self-knowledge.md", status: "active", size_bytes: 3890, lines: 98, last_updated: "2026-06-16" },
#     ...
#   ]
# }

# Get page 2
memory_get(action: "browse", page: 2, results_per_page: 10)
```

Browse behavior:
- `results_per_page` defaults to 10, max 50.
- Files are sorted by `last_updated` (most recent first).
- Each entry includes `status` (active/processed/unprocessed) so agents can identify files that need consolidation.
- `total_files` and `total_pages` enable agents to decide whether to browse further or search specifically.

### Implementation notes

- The `lines` parameter in `memory_get` accepts both integer (existing behavior: `from` + `lines` count) and string range (`"50-80"` syntax).
- Regex search uses Ruby's `Regexp` class. Invalid regex patterns return a clear error message rather than failing silently.
- Browse results include `size_bytes` and `lines` to help agents decide whether to read a full file or target specific sections.
- All three modes (lines, regex, browse) work on the `memory_files` table content, not on filesystem paths — this ensures they work correctly for migrated content that may not have a 1:1 filesystem mapping.
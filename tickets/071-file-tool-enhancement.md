# Ticket 071 — File Tool Enhancement (Pagination, searchLines, insertLines, replaceLines, appendLines, deleteLines)

**Created:** 2026-06-05
**Status:** Implemented (2026-07-08, commit 3f1efcf)
**Report:** [071-implementation-report.md](071-implementation-report.md)
**Priority:** P2
**Depends on:** FileTool (existing)

## Summary

Enhance the existing `FileTool` with six capabilities: read pagination, in-file search, line insertion, block replacement, line deletion, and appending. These are common operations that currently require reading the full file, doing string manipulation in the agent's context, and writing back — wasting context window space and risking errors on large files.

## Current State

The existing `FileTool` supports three actions:

| Action | Description |
|--------|-------------|
| `read` | Read file contents (no pagination — returns entire file or offset+limit) |
| `write` | Write content to file (overwrites entire file) |
| `edit` | Find-and-replace exact text match |

The `read` action already supports `offset` and `limit` parameters for partial reads. However, the agent has to manually manage pagination state — there's no "page" abstraction.

The `edit` action requires exact string matching, which is fragile and verbose for large replacements.

There's no way to insert lines at a specific position without reading the whole file, splitting it, and rewriting it.

## Proposed Changes

### 1. searchLines

Search within a file for a pattern (regex or substring) and return matching line numbers with context. This is the most common missing operation — currently the agent must `read` an entire file into context, visually scan for the relevant section, then perform an edit. `searchLines` lets the agent locate the target lines first, then `replaceLines` or `read` just that range.

```json
{
  "action": "searchLines",
  "path": "/path/to/file.cpp",
  "pattern": "ratchet_encrypt",
  "context": 3
}
```

- `pattern`: Regex pattern or plain substring to search for. If the pattern contains regex metacharacters and `regex` is false, it's treated as a literal substring.
- `regex` (default `false`): If true, `pattern` is treated as a regular expression. If false, treated as a literal substring.
- `context` (default `0`): Number of lines before and after each match to include.
- `max_matches` (default `50`): Maximum number of matches to return.

**Response format:**

```json
{
  "path": "/path/to/file.cpp",
  "total_lines": 342,
  "matches": [
    {
      "line": 147,
      "text": "std::vector<uint8_t> ratchet_encrypt(const SessionState& state, const std::string& plaintext) {",
      "context_before": ["}", "", "// Ratchet encrypt"],
      "context_after": ["    auto [messageKey, macKey, iv] = deriveWhisperMessageKeys(state);", "    auto ciphertext = aes256CbcEncrypt(messageKey, iv, paddedPlaintext);", ...]
    },
    ...
  ],
  "match_count": 3,
  "truncated": false
}
```

**Use cases:**
- Find where a function is defined before editing it
- Locate all references to a variable or symbol
- Find config entries by key name
- Identify which lines contain a pattern before using `replaceLines`

### 2. findBlock

Search for a phrase within a file, then expand each match to its enclosing block by scanning backwards for a `blockStart` pattern and forwards for a `blockEnd` pattern. This is the most contextually useful search operation — it answers "what function/class/section does this reference belong to?"

```json
{
  "action": "findBlock",
  "path": "/path/to/file.cpp",
  "phrase": "ratchet_encrypt",
  "blockStart": "^(void|int|static|inline|auto)\\s+\\w+",
  "blockEnd": "^}",
  "nesting": true
}
```

- `phrase`: Substring or regex pattern to search for within the file. Same as `searchLines` pattern — literal by default, regex if `regex: true`.
- `blockStart`: Regex pattern for the start of the enclosing block. The tool scans **backwards** from each phrase match line until it finds a line matching this pattern.
- `blockEnd`: Regex pattern for the end of the enclosing block. The tool scans **forwards** from each phrase match line until it finds a line matching this pattern.
- `nesting` (default `false`): If true, count nesting depth for `blockEnd` matches. Use when `blockEnd` is a symmetrical delimiter like `}`. When nesting is enabled, a match on `blockEnd` only counts if the nesting depth is zero (i.e., the delimiter closes the block opened by `blockStart`).
- `regex` (default `false`): If true, `phrase` is treated as a regular expression.
- `max_blocks` (default `10`): Maximum number of blocks to return.

**Nesting depth counting:** When `nesting` is true, the tool counts opening delimiters (lines matching `blockStart` or containing `{`) and closing delimiters (lines containing `}`). The block end is found when the closing delimiter brings the depth back to zero.

**Python-style indentation blocks:** For languages without braces, use `blockStart: "^def |^class |^async def "` and `blockEnd: "^\\S"` (next line at column 0). No nesting needed.

**XML/HTML blocks:** Use `blockStart: "<section|<config|<module"` and `blockEnd: "</section>|</config>|</module>"` with nesting enabled.

**Response format:**

```json
{
  "path": "/path/to/file.cpp",
  "total_lines": 342,
  "blocks": [
    {
      "phrase_line": 147,
      "phrase_text": "std::vector<uint8_t> ratchet_encrypt(const SessionState& state, const std::string& plaintext) {",
      "block_start_line": 142,
      "block_start_text": "std::vector<uint8_t> encrypt_1to1(const Session& session, const std::string& plaintext) {",
      "block_end_line": 168,
      "block_end_text": "}",
      "content": "...lines 142-168..."
    }
  ],
  "match_count": 1
}
```

**Use cases:**
- Find which function contains a specific call or variable reference
- Find which class definition contains a method
- Find which config section contains a specific key
- Find which test case contains a specific assertion
- Locate the enclosing scope for targeted replacement via `replaceLines`

**Workflow with replaceLines:**
```
findBlock("ratchet_encrypt", blockStart="^(void|int|static)", blockEnd="^}", nesting=true)
  → block_start_line=142, block_end_line=168
replaceLines(142, 168, new_implementation)
  → Function replaced in two tool calls, no full-file read needed
```

### 3. Read Pagination

Add page-based read access. Instead of tracking byte offsets and line counts manually, the agent can request pages.

**New parameters for `read`:**

```json
{
  "action": "read",
  "path": "/path/to/file.cpp",
  "page": 1,
  "page_size": 50
}
```

- `page` (1-based): Which page to return. Default is 1.
- `page_size`: Lines per page. Default 50, max 200.
- Returns total line count and total pages so the agent can navigate.

**Response format:**

```json
{
  "path": "/path/to/file.cpp",
  "page": 1,
  "page_size": 50,
  "total_lines": 342,
  "total_pages": 7,
  "content": "...lines 1-50...",
  "truncated": false
}
```

When both `offset`/`limit` and `page`/`page_size` are provided, `page`/`page_size` takes precedence. When neither is provided, the full file is returned (capped at 2000 lines with `truncated: true`).

### 3. insertLines

Insert new lines at a specific line number. The existing content from that line onward shifts down.

```json
{
  "action": "insertLines",
  "path": "/path/to/file.cpp",
  "at_line": 42,
  "content": "    // New comment\n    int newVar = 0;\n"
}
```

- `at_line` (1-based): Insert before this line. `at_line: 1` inserts at the top. `at_line: N` where N > total_lines appends at the end.
- `content`: The lines to insert. Must include trailing newline if the insertion should end with a line break.
- Returns the number of lines inserted and the new total line count.

**Use cases:**
- Adding a new function after line 150
- Inserting an `#include` at the top of a file
- Adding a new route to a router file at a specific position
- Inserting a section into a config file

**Implementation:**
1. Read the file
2. Split into lines
3. Insert `content` lines at position `at_line - 1`
4. Join and write back
5. Return `{lines_inserted: N, total_lines: M}`

### 4. replaceLines

Replace a contiguous block of lines (from line X to line Y) with new content. This is the block-level equivalent of the existing string-level `edit` action.

```json
{
  "action": "replaceLines",
  "path": "/path/to/file.cpp",
  "start_line": 42,
  "end_line": 55,
  "content": "    // Replaced block\n    for (int i = 0; i < 10; i++) {\n        printf(\"%d\\n\", i);\n    }\n"
}
```

- `start_line` (1-based, inclusive): First line to replace.
- `end_line` (1-based, inclusive): Last line to replace. Must be >= `start_line`.
- `content`: The replacement lines. Can be empty string to delete lines.
- Returns the number of lines removed, lines added, and new total line count.

**Use cases:**
- Replacing a function body (lines 42-55) with a new implementation
- Deleting a block of code (set `content` to `""`)
- Replacing a config section with updated values
- Swapping out an entire class definition

**Implementation:**
1. Read the file
2. Split into lines
3. Remove lines `start_line` through `end_line` (1-based, inclusive)
4. Insert `content` lines at the removal position
5. Join and write back
6. Return `{lines_removed: N, lines_added: M, total_lines: K}`

**Safety:** If the file has changed since the agent last read it (detected via line count or content hash mismatch), the operation should fail with a clear error message suggesting re-read the file first. This prevents overwriting concurrent edits.

### 5. deleteLines

Delete a contiguous block of lines. This is an explicit form of `replaceLines` with empty content — clearer intent, no ambiguity about what passing empty content means.

```json
{
  "action": "deleteLines",
  "path": "/path/to/file.cpp",
  "start_line": 42,
  "end_line": 55
}
```

- `start_line` (1-based, inclusive): First line to delete.
- `end_line` (1-based, inclusive): Last line to delete. Must be >= `start_line`.
- Returns the number of lines deleted and the new total line count.

**Why separate from `replaceLines`?** Explicit intent. An agent calling `deleteLines(42, 55)` clearly means "remove these lines." An agent calling `replaceLines(42, 55, "")` is ambiguous — did it intend to delete, or is the empty string a bug? Making deletion a first-class action eliminates this ambiguity and makes tool call logs easier to audit.

### 7. appendLines

Append lines to the end of a file without reading it first. This is the most common write pattern — adding to logs, appending to lists, adding config entries — and it currently requires reading the entire file just to add something at the bottom.

```json
{
  "action": "appendLines",
  "path": "/path/to/log.md",
  "content": "\n## 2026-06-06\n\nNew entry for today.\n"
}
```

- `content`: The lines to append. Added after the last line of the file.
- If the file doesn't end with a newline, a newline is prepended to `content` before appending.
- Returns the number of lines appended and the new total line count.
- **Does not require reading the file first.** Opens in append mode, writes, closes.

**Use cases:**
- Adding daily log entries to a journal file
- Appending to a `.gitignore`
- Adding a new entry to a JSON or YAML list (with proper formatting)
- Appending to memory files
- Adding new routes to a router config

## Updated Tool Definition

```json
{
  "name": "file",
  "description": "Read, write, and edit files. Supports pagination, search, block finding, line insertion, block replacement, deletion, and appending.",
  "parameters": {
    "action": {
      "type": "string",
      "enum": ["read", "write", "edit", "searchLines", "findBlock", "insertLines", "replaceLines", "deleteLines", "appendLines"],
      "description": "Action to perform."
    },
    "path": {
      "type": "string",
      "description": "File path (relative to workspace root or absolute)."
    },
    "content": {
      "type": "string",
      "description": "Content to write, insert, replace, or append. For 'write', full file content. For 'insertLines' and 'replaceLines', the new content. For 'appendLines', lines to add. For 'deleteLines', ignored."
    },
    "offset": {
      "type": "integer",
      "description": "Line number to start reading from (1-based, for 'read')."
    },
    "limit": {
      "type": "integer",
      "description": "Number of lines to read (for 'read')."
    },
    "page": {
      "type": "integer",
      "description": "Page number for paginated reading (1-based). Overrides offset/limit."
    },
    "page_size": {
      "type": "integer",
      "description": "Lines per page for paginated reading (default 50, max 200)."
    },
    "oldText": {
      "type": "string",
      "description": "Exact text to find and replace (for 'edit'). Must be unique in the file."
    },
    "newText": {
      "type": "string",
      "description": "Replacement text (for 'edit')."
    },
    "pattern": {
      "type": "string",
      "description": "Search pattern for 'searchLines'. Literal substring by default, regex if regex=true."
    },
    "regex": {
      "type": "boolean",
      "description": "Treat pattern as a regular expression (for 'searchLines'). Default false."
    },
    "context": {
      "type": "integer",
      "description": "Lines of context before and after each match (for 'searchLines'). Default 0."
    },
    "max_matches": {
      "type": "integer",
      "description": "Maximum number of matches to return (for 'searchLines' and 'findBlock'). Default 50 for searchLines, 10 for findBlock."
    },
    "blockStart": {
      "type": "string",
      "description": "Regex pattern for the start of the enclosing block (for 'findBlock'). Tool scans backwards from phrase match."
    },
    "blockEnd": {
      "type": "string",
      "description": "Regex pattern for the end of the enclosing block (for 'findBlock'). Tool scans forwards from phrase match."
    },
    "nesting": {
      "type": "boolean",
      "description": "Count nesting depth for blockEnd (for 'findBlock'). Use when blockEnd is a symmetrical delimiter like }. Default false."
    },
    "at_line": {
      "type": "integer",
      "description": "Line number to insert before (1-based, for 'insertLines')."
    },
    "start_line": {
      "type": "integer",
      "description": "First line to replace or delete (1-based, inclusive, for 'replaceLines' and 'deleteLines')."
    },
    "end_line": {
      "type": "integer",
      "description": "Last line to replace or delete (1-based, inclusive, for 'replaceLines' and 'deleteLines')."
    }
  }
}
```

## Scope

### v1 (This ticket) — ✅ Complete
- [x] `searchLines` — search within a file (substring or regex), return matching line numbers + context
- [x] `findBlock` — search for phrase, expand to enclosing block (backwards scan for blockStart, forwards scan for blockEnd, optional nesting depth)
- [x] `read` pagination — `page`/`page_size` parameters, return pagination metadata
- [x] `insertLines` — insert content at a specific line number
- [x] `replaceLines` — replace a block of lines by start/end line numbers
- [x] `deleteLines` — delete a block of lines (explicit intent, no empty-string ambiguity)
- [x] `appendLines` — append to end of file without reading first (append-mode write)
- [ ] Content hash or line count check for concurrent edit detection (deferred — single-agent per file)
- [x] Binary file detection — refuse to edit binary files with a clear error message
- [x] Update tool definition in `FileTool::GetDefinition()`
- [ ] Integration tests (deferred — manual testing passed)

### v2 (Future)
- [ ] `moveLines` — atomic cut-and-paste within a file (delete block from source, insert at target)
- [ ] `editLines` — string-based find/replace scoped to a line range (combines `searchLines` + `replaceLines`)
- [ ] `watchLines` — subscribe to file changes within a line range (for passive mode awareness)
- [ ] `diffLines` — compare two versions of a file, return structured diff

## Hard-Won Lessons (Predicted)

1. **Line numbers are 1-based for humans, 0-based for arrays.** Every off-by-one error in line manipulation is a file corruption. Use 1-based in the API, convert to 0-based internally, document clearly.
2. **Concurrent edits are a real problem.** If the agent reads a file, decides to replace lines 42-55, but the file was modified between read and replace, the line numbers may no longer correspond to the intended content. Content hash verification prevents silent corruption.
3. **Empty string content means delete.** `replaceLines(start_line=5, end_line=10, content="")` deletes lines 5-10. This should be documented explicitly so agents don't accidentally delete lines by passing empty content.
4. **Trailing newlines matter.** Files that end with a newline vs. files that don't. Always preserve the file's original trailing newline convention when inserting/replacing.
5. **Large files need pagination.** A 10,000-line file should never be returned in full to the agent. Default page size of 50 lines keeps context window usage reasonable.
6. **searchLines is the most-used action.** The typical workflow is: search → locate → read range → edit. Without search, the agent reads the whole file into context to find 3 relevant lines. With search, the agent never needs to load more than a page at a time.
7. **appendLines should not read the file.** Opening in append mode and writing is O(1). Reading the entire file to append is O(n). For log files and memory files that grow monotonically, this matters.
8. **deleteLines is clearer than replaceLines with empty content.** When auditing tool call logs, `deleteLines(42, 55)` is unambiguous. `replaceLines(42, 55, "")` could be a bug or intentional. First-class actions for common operations reduce ambiguity.
9. **findBlock solves the "what scope does this belong to?" problem.** searchLines tells you where a phrase appears, but not its enclosing context. findBlock gives you the block boundaries, so you can immediately call replaceLines on the right range. The nesting depth option handles C/C++/Java/TypeScript brace blocks. For indentation-based languages (Python, YAML), use blockEnd patterns that match dedentation.
10. **findBlock + replaceLines is the tightest edit loop.** findBlock → get block_start_line and block_end_line → replaceLines with new content. Two tool calls to surgically replace a function, class, or config section. No context window waste.
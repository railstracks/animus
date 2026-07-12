# Ticket 023 — Progress Report: Web Tools (HTTP Client + Search + Fetch)

**Date:** 2026-05-05  
**Status:** Partially Complete

---

## Summary

Ticket 023 has been partially delivered.

Implemented:

1. Shared `HttpClient` infrastructure with SSRF controls and response limits.
2. `http` tool for raw HTTP interaction.
3. `web_fetch` tool for URL retrieval with HTML-to-text/markdown conversion and truncation.

Not implemented:

1. `web_search` tool.
2. Search provider integration/rate-limit plumbing for search.
3. Some advanced fetch behaviors described in ticket notes (robots/caching semantics as specified).

---

## Implemented Scope

### 1. Shared HTTP client

- Implemented `HttpClient` with:
  - configurable allowlist/denylist checks
  - private-address blocking (SSRF mitigation) with explicit override
  - redirect following controls
  - per-request timeout support
  - max response body size truncation
  - TLS verification toggle

**Files:**
- `include/animus_kernel/tools/HttpClient.h`
- `src/kernel/tools/HttpClient.cpp`

### 2. `http` tool

- Supports methods:
  - `GET`, `POST`, `PUT`, `PATCH`, `DELETE`, `HEAD`
- Parses optional headers/body/timeout.
- Returns structured payload:
  - `status_code`
  - `headers`
  - `body`
  - `elapsed_ms`
- Uses shared `HttpClient` policy enforcement.

**File:**
- `src/kernel/tools/HttpTool.cpp`

### 3. `web_fetch` tool

- Fetches URL through shared `HttpClient`.
- Supports output formats:
  - `markdown`
  - `text`
  - `raw`
- Includes format auto-detection behavior for non-HTML content.
- Converts HTML to readable text/markdown.
- Applies `max_length` truncation.

**File:**
- `src/kernel/tools/WebFetchTool.cpp`

### 4. Tool registration

- `http` and `web_fetch` are registered as built-in tools.

**File:**
- `src/kernel/AgentKernel.cpp`

---

## Not Yet Implemented (from Ticket 023 definition)

1. `web_search` tool and provider abstraction usage for search APIs.
2. Structured `web_search` results (`title`, `url`, `snippet`, `published_date`).
3. Freshness filter behavior for search.
4. Declared short-TTL caching layer for `web_fetch` (as explicitly scoped in ticket text).
5. Explicit robots/rate-limit behavior coupling for fetch/search as described in ticket notes.

---

## Acceptance Criteria Mapping

- Shared HttpClient with timeout support: **Implemented**
- URL allowlist/denylist on outbound requests: **Implemented**
- Response size limit (default 1MB): **Implemented**
- Redirect following/max redirects: **Implemented**
- `http` supports required HTTP methods: **Implemented**
- `http` returns structured response: **Implemented**
- `web_search` structured results: **Not Implemented**
- `web_search` freshness filtering: **Not Implemented**
- Provider-agnostic `web_search`: **Not Implemented**
- `web_fetch` readable extraction for HTML: **Implemented**
- `web_fetch` raw mode: **Implemented**
- `web_fetch` max_length truncation: **Implemented**
- `web_fetch` content-type based format behavior: **Implemented**
- `web_fetch` short TTL cache: **Not Implemented**
- All listed tools registered/callable: **Partially Implemented**

---

## Notes

Ticket 023’s original broader “communication + web” scope was already explicitly narrowed to web tools.  
Within that narrowed scope, the shipped subset is production-usable for direct HTTP calls and page fetch/extraction, but search remains an outstanding gap.


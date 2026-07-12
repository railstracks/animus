# Ticket 069 — Web Search Tool: Implementation Report

**Status:** ✅ Implemented  
**Date:** 2026-07-12  
**Branch:** main (single-commit history)

## Summary

Implemented a provider-agnostic web search tool for Animus agents, with Brave Search as the first provider. The tool allows agents to search the web for current information, returning titles, URLs, and snippets. Includes a full Admin UI configuration page with test-search functionality and persistent config storage.

## Files Created

### C++ Kernel
- `include/animus_kernel/tools/WebSearchTool.h` (91 lines) — `ISearchProvider` interface, `SearchQuery`/`SearchResult` types, `BraveSearchProvider`, `WebSearchTool : IToolHandler`
- `src/kernel/tools/WebSearchTool.cpp` (290 lines) — Brave Search API implementation (URL encoding, rate limiting, JSON parsing), tool definition with `query`/`count`/`freshness` params, execution and result formatting

### Admin Server
- `src/kernel/admin/internal/AdminServerRoutesSearch.inc` (170 lines) — Three API endpoints:
  - `GET /api/v1/search/config` — current config (masked API key)
  - `PUT /api/v1/search/config` — update config + persist to AgentConfigStore
  - `POST /api/v1/search/test` — run a live test query

### Admin UI
- `admin-ui/src/views/WebSearchView.vue` (222 lines) — Configuration form (provider, API key, default count, rate limit, endpoint) + test search panel with live results display
- `admin-ui/src/i18n/locales/en/webSearch.ts` (19 keys) — English i18n strings

## Files Modified

### Kernel
- `src/kernel/AgentKernel.cpp` — WebSearchTool include, config loading from AgentConfigStore at startup (`__kernel__` scope), tool registration conditional on API key presence, `json/json.h` include
- `src/kernel/admin/AdminServerRoutes.cpp` — `WebSearchTool.h` include for test-search route types
- `include/animus_kernel/AdminServer.h` — `m_searchConfig` pointer, `m_httpClientPtr`, `SetSearchConfig()`, `SetHttpClient()`, `RegisterRoutesSearch()` declaration

### Build
- `CMakeLists.txt` — Added `src/kernel/tools/WebSearchTool.cpp` to sources
- `README.md` — Added web_search to tools list

### Admin UI Wiring
- `admin-ui/src/router/index.ts` — `/web-search` route
- `admin-ui/src/components/AppSidebar.vue` — "Web Search" nav entry with `mdi-magnify` icon
- `admin-ui/src/i18n/locales/en/index.ts` — webSearch import
- `admin-ui/src/i18n/locales/en/sidebar.ts` — sidebar label

## Architecture

```
Agent calls web_search tool
        │
        ▼
  WebSearchTool::Execute()
        │
        ▼
  ISearchProvider::Search()
        │
        ▼
  BraveSearchProvider
    ├── HttpClient (shared with HttpTool, WebFetchTool)
    ├── Rate limiting (sliding window, configurable)
    ├── Brave Search API v1
    └── JSON response → SearchResult vector
```

### Config Persistence

Search config is stored in `AgentConfigStore` under `agentId="__kernel__"`, key=`"search_config"` as a JSON blob. This survives restarts. On startup, AgentKernel loads the persisted config before tool registration.

### Provider Abstraction

`ISearchProvider` interface allows future providers (Google, SearXNG, DuckDuckGo) without changing the tool. The Admin UI `POST /api/v1/search/test` endpoint instantiates a `BraveSearchProvider` directly for testing.

## Bugs Fixed During Review

1. **Duplicate function in WebSearchTool.cpp** — `FreshnessToBrave` had a leftover orphan declaration from development; cleaned up
2. **Non-existent `SetGlobalSetting` call** — Admin routes used `SetGlobalSetting` which doesn't exist on `AgentConfigStore`; fixed to use `Set("__kernel__", "search_config", ...)`
3. **Missing includes** — Added `<json/json.h>` to AgentKernel.cpp and `WebSearchTool.h` to AdminServerRoutes.cpp

## Usage

Agents call:
```json
{
  "name": "web_search",
  "arguments": {
    "query": "latest AI news",
    "count": 5,
    "freshness": "week"
  }
}
```

Result (delivered to model):
```json
{
  "query": "latest AI news",
  "count": 5,
  "results": [
    {"title": "...", "url": "...", "snippet": "...", "published_date": "..."}
  ]
}
```

## What's Not Included (Future)

- **Country/language params** — SearchQuery supports them, provider sends them, but tool definition doesn't expose them (can add later)
- **Additional providers** — SearXNG (self-hosted), Google, DuckDuckGo
- **Caching** — No result caching; every search hits the API
- **Config file parsing** — Search config from CLI flags or config file (Admin UI is the primary config path)

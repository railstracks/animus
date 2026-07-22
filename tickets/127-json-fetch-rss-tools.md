# Ticket 127: Stored Links Tool + RSS Tool

**Status:** Draft
**Date:** 2026-07-22
**Author:** Melvin + Kestrel

## Problem

Agents need structured data from external sources for analysis workflows (market assessment SOPs, research tasks). Currently they only have `web_fetch` (returns markdown/text, strips structure) and `web_search` (returns search results, not page content).

Two gaps:

1. **Pre-configured HTTP endpoints** — Many data sources return structured data (JSON, CSV, etc.). `web_fetch` discards structure by extracting readable text. A generic tool that fetches pre-configured HTTP links by identifier would let agents pull structured responses without hardcoding URLs in prompts. The operator curates which endpoints are available; the agent selects by name.

2. **RSS/news feeds** — News aggregation is a common analysis input. Agents need itemized feed results with metadata (title, link, date, paywall indication) rather than raw XML. Configurable RSS feeds let operators define which sources are available without the agent guessing URLs.

## Design

### Tool 1: `stored_links`

**Purpose:** Fetch HTTP responses from pre-configured stored links. The agent selects from available stored links by identifier; the tool fetches the URL and returns the raw response.

**Why stored links instead of freeform URLs:**
- Operators control which endpoints are available (security, rate limiting)
- Agent doesn't need to know URL templates or API keys
- Configuration defines human-readable identifiers ("sp500-4h") that map to full URLs
- Keeps the tool domain-agnostic — any HTTP endpoint works

**Configuration:**

Stored links are configured in `KernelConfig`:

```json
{
  "stored_links": [
    {
      "id": "sp500-4h",
      "label": "S&P 500 Index, 4-Hour Intervals",
      "url": "https://example.com/api?symbol=SPX&interval=4h"
    },
    {
      "id": "sp500-1h",
      "label": "S&P 500 Index, 1-Hour Intervals",
      "url": "https://example.com/api?symbol=SPX&interval=1h"
    }
  ]
}
```

**Tool schema:**

```json
{
  "name": "stored_links",
  "description": "Fetch pre-configured HTTP links by identifier. Returns the raw response content.",
  "parameters": {
    "type": "object",
    "properties": {
      "id": {
        "type": "string",
        "description": "Identifier of the stored link to fetch. Use 'list' to see available stored links."
      }
    },
    "required": ["id"]
  }
}
```

**Behavior:**
- `stored_links("list")` — returns a list of available stored links with id, label
- `stored_links("sp500-4h")` — fetches the URL, returns raw response body
- Content-Type header is included in the result so the agent knows what format came back
- Uses the existing `HttpClient` with SSRF protection (inherited from framework)
- Timeout: 15s default, configurable

**Session types:** `["default"]` — available in normal chat sessions.

**No freeform URL parameter:** The tool only fetches pre-configured URLs. This is deliberate — it prevents prompt injection from causing the agent to fetch arbitrary internal endpoints.

### Tool 2: `rss`

**Purpose:** Fetch and parse RSS/Atom feeds from pre-configured sources. Returns itemized results with metadata.

**Configuration:**

RSS feeds are configured in `KernelConfig`:

```json
{
  "rss_feeds": [
    {
      "id": "market-news",
      "label": "Market News",
      "url": "https://feeds.bloomberg.com/markets.rss"
    },
    {
      "id": "tech-news",
      "label": "Technology News",
      "url": "https://feeds.arstechnica.com/arstechnica/index.rss"
    }
  ]
}
```

**Tool schema:**

```json
{
  "name": "rss",
  "description": "Fetch RSS/Atom feeds. Returns itemized list of articles with title, link, date, and summary.",
  "parameters": {
    "type": "object",
    "properties": {
      "feed": {
        "type": "string",
        "description": "Feed identifier to fetch. Omit or use 'all' to fetch all configured feeds."
      },
      "limit": {
        "type": "integer",
        "description": "Maximum items per feed (default: 10)"
      }
    }
  }
}
```

**Behavior:**
- `rss()` or `rss("all")` — fetches all configured feeds, returns merged itemized list
- `rss("market-news")` — fetches one feed
- `rss("market-news", 5)` — limits to 5 items
- Each item includes: `title`, `link`, `pub_date`, `description` (truncated to ~500 chars), `feed_id`, `feed_label`
- XML parsing: minimal RSS 2.0 + Atom 1.0 parser (no external dependency — parse with a small inline XML parser or use expat if already available)
- Items sorted by `pub_date` descending across feeds
- Failed feeds are noted but don't fail the whole call (graceful degradation)
- Timeout: 10s per feed

**Session types:** `["default"]` — available in normal chat sessions.

## Scope

### New files:
- `include/animus_kernel/StoredLinksTool.h`
- `src/kernel/tools/StoredLinksTool.cpp`
- `include/animus_kernel/RssTool.h`
- `src/kernel/tools/RssTool.cpp`

### Modified files:
- `include/animus_kernel/KernelConfig.h` — add `stored_links` and `rss_feeds` config fields
- `src/kernel/AgentKernel.cpp` — register both tools

### No admin UI changes in this phase:
- Configuration via KernelConfig JSON (manual edit for now)
- Admin UI for managing stored links and RSS feeds can come in a follow-up

## Testing

1. Configure 8 stored links at different intervals for a target symbol
2. Agent calls `stored_links("list")` — sees all 8
3. Agent calls `stored_links("sp500-4h")` — gets response with content and content-type
4. Configure 2-3 RSS feeds
5. Agent calls `rss()` — gets merged, sorted items from all feeds
6. Agent calls `rss("market-news", 5)` — gets 5 items from one feed
7. Verify paywalled sources return gracefully (link included, description may be limited)
8. Verify SSRF protection blocks internal addresses
9. Verify timeout handling on slow endpoints
# Ticket 069 — Web Search Tool (Provider-Agnostic)

**Created:** 2026-06-05
**Status:** Draft (updates ticket 023)
**Priority:** P1
**Depends on:** HttpClient (existing)

## Summary

Implement the `web_search` tool from ticket 023. The HTTP client (`HttpTool`) and content fetcher (`WebFetchTool`) already exist in Animus. This ticket covers the missing piece: a provider-agnostic web search tool that queries external search APIs and returns structured results.

Ticket 023 already has a detailed design. This ticket updates it with concrete provider implementations and current architecture context.

## Current State

- **HttpTool** — ✅ Implemented. General-purpose HTTP client with method support, headers, body, timeout.
- **WebFetchTool** — ✅ Implemented. Fetches URLs, extracts readable content as markdown/text, respects robots.txt, handles redirects.
- **Web Search** — ❌ Not implemented. The `web_search` action from ticket 023 doesn't exist yet.

## Provider Architecture

```cpp
class SearchProvider {
public:
    virtual ~SearchProvider() = default;
    virtual std::string Name() const = 0;
    virtual SearchResults Search(const SearchQuery& query) = 0;
};

struct SearchQuery {
    std::string query;
    int count{5};
    std::optional<std::string> freshness;  // "day", "week", "month", "year"
    std::optional<std::string> country;     // ISO 3166-1 alpha-2
    std::optional<std::string> language;    // ISO 639-1
};

struct SearchResult {
    std::string title;
    std::string url;
    std::string snippet;
    std::optional<std::string> published_date;
};

using SearchResults = std::vector<SearchResult>;
```

## Provider Implementations

### Priority 1: Brave Search
- **Why first:** Free tier (2000 queries/month), good quality, simple API, no OAuth
- **API:** `GET https://api.search.brave.com/res/v1/web/search?q={query}&count={count}`
- **Auth:** API key in `X-Subscription-Token` header
- **Freshness:** `&freshness={pw|pm|py}` parameter
- **Rate limit:** 1 QPS on free tier, 5 QPS on paid

### Priority 2: SearXNG (self-hosted)
- **Why second:** Self-hosted, no API key, no cost, privacy-first, supports multiple backends
- **API:** `GET http://localhost:8888/search?q={query}&format=json&categories=general`
- **Auth:** None (or basic auth if configured)
- **Freshness:** `&time_range={day|week|month|year}`
- **Rate limit:** Self-imposed (be nice to the backends)
- **Key advantage:** Zero external dependency. Runs on the same machine as Animus.

### Priority 3: Google Custom Search
- **Why third:** Good quality but requires setup (custom search engine ID + API key)
- **API:** `GET https://www.googleapis.com/customsearch/v1?q={query}&cx={engine_id}&key={api_key}`
- **Auth:** API key in query parameter
- **Rate limit:** 100 queries/day on free tier

### Priority 4: Kagi Search
- **Why fourth:** High quality, paid, good for premium deployments
- **API:** `GET https://kagi.com/api/v0/search?q={query}&limit={count}`
- **Auth:** API key in `Authorization` header

### Priority 5: Perplexity (via API)
- **Why fifth:** AI-augmented search, good for research queries
- **API:** Different shape — returns synthesized answers, not just links
- **Auth:** API key

## Configuration

```json
{
  "tools": {
    "web_search": {
      "provider": "brave",
      "providers": {
        "brave": {
          "api_key": "${BRAVE_SEARCH_API_KEY}",
          "rate_limit_per_minute": 10
        },
        "searxng": {
          "endpoint": "http://localhost:8888",
          "rate_limit_per_minute": 30
        },
        "google": {
          "api_key": "${GOOGLE_SEARCH_API_KEY}",
          "engine_id": "${GOOGLE_SEARCH_ENGINE_ID}",
          "rate_limit_per_minute": 5
        },
        "kagi": {
          "api_key": "${KAGI_API_KEY}",
          "rate_limit_per_minute": 10
        }
      },
      "default_count": 5,
      "max_count": 20,
      "cache_ttl_seconds": 300
    }
  }
}
```

The `provider` field selects the active provider. Agents can override per-call if multiple are configured.

## Tool Definition

```json
{
  "name": "web_search",
  "description": "Search the web for information. Returns titles, URLs, and snippets for the top results. Use web_search for finding information, web_fetch for reading a specific URL.",
  "parameters": {
    "query": {
      "type": "string",
      "description": "Search query string"
    },
    "count": {
      "type": "integer",
      "description": "Number of results (1-20, default 5)"
    },
    "freshness": {
      "type": "string",
      "enum": ["day", "week", "month", "year"],
      "description": "Filter results by recency. Omit for no time filter."
    },
    "provider": {
      "type": "string",
      "description": "Override the configured search provider for this call."
    }
  }
}
```

## Scope

### v1 (This ticket)
- [ ] SearchProvider interface + SearchQuery/SearchResult types
- [ ] Brave Search provider
- [ ] SearXNG provider (self-hosted)
- [ ] web_search tool registration and execution
- [ ] Rate limiting (per-provider)
- [ ] Result caching (TTL: 5 minutes for identical queries)
- [ ] Provider configuration in kernel config
- [ ] Admin UI for search provider config
- [ ] Integration tests with Brave (API key) and SearXNG (local)

### v2 (Future)
- [ ] Google Custom Search provider
- [ ] Kagi provider
- [ ] Perplexity provider (different result shape)
- [ ] Multi-provider fallback (try next provider on rate limit/error)
- [ ] Domain filtering (restrict to specific domains)
- [ ] Search suggestions / auto-complete
- [ ] Image search variant
- [ ] News search variant (when provider supports it)

## Relationship to Other Tickets

- **Ticket 023** — Original design. This ticket replaces the web search portion.
- **HttpTool** — Uses the shared `HttpClient` for outbound requests.
- **WebFetchTool** — Companion tool for reading URLs found via search. Already implemented.
- **Image Tool (068)** — May need web_search for finding image URLs (future).

## Notes

The key design principle is **provider agnosticism**. The tool returns `SearchResult` objects regardless of which provider handled the query. This means agents don't need to know which search engine is configured — they just call `web_search` and get results. Provider selection is an admin concern, not an agent concern.
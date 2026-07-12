# Ticket 023 — Web Tools (HTTP Client + Search + Fetch)

**Priority:** P1 (immediate value, no dependencies beyond 021)
**Status:** Open
**Dependencies:** Ticket 021 (Tool Calling and Reasoning Mode)
**Updated:** 2026-05-03

## Summary

Implement a shared HTTP client infrastructure and three web tools on top of it: general-purpose HTTP requests, web search via configured providers, and URL content fetching with HTML-to-markdown extraction. These are immediate value-adds with no external dependencies.

## Context

This ticket was originally "Communication & Web Tools" covering 6 tools. It has been rescoped to the web-only tools after discussion (May 3). Communication tools (`reply_channel`, `notify`) are deferred to the Channel Architecture ticket (025). `email` is split into its own ticket (026) due to its large testing surface.

## Shared Infrastructure: HTTP Client

All three tools share a common HTTP client. Build this once as a reusable component.

```cpp
class HttpClient {
public:
    struct Response {
        int status_code;
        std::map<std::string, std::string> headers;
        std::string body;
        uint64_t elapsed_ms;
    };

    struct Request {
        std::string method;  // GET, POST, PUT, PATCH, DELETE, HEAD
        std::string url;
        std::map<std::string, std::string> headers;
        std::string body;
        int timeout_seconds{30};
    };

    Response execute(const Request& req);

    // URL allowlist/denylist for SSRF prevention
    void setAllowlist(const std::vector<std::string>& patterns);
    void setDenylist(const std::vector<std::string>& patterns);
    bool isUrlAllowed(const std::string& url) const;
};
```

**Implementation notes:**
- Uses CPR (C++ Requests) or libcurl under the hood
- Connection pooling for repeated requests to the same host
- Configurable per-request timeouts with sensible defaults
- Response body size limit (configurable, default 1MB)
- Redirect following with max-redirect limit (default 5)
- URL allowlist/denylist evaluation before each request (SSRF prevention)
- Thread-safe — multiple tools may make concurrent requests

## Tools

### `http`

General-purpose HTTP client for API calls, webhooks, and service interaction.

```json
{
    "name": "http",
    "description": "Make an HTTP request. Use for API calls, webhooks, health checks, and any HTTP interaction that doesn't fit web_search or web_fetch.",
    "parameters": {
        "method": {
            "type": "string",
            "enum": ["GET", "POST", "PUT", "PATCH", "DELETE", "HEAD"],
            "description": "HTTP method"
        },
        "url": {
            "type": "string",
            "description": "Request URL"
        },
        "headers": {
            "type": "object",
            "description": "Request headers as key-value pairs"
        },
        "body": {
            "type": "string",
            "description": "Request body (JSON string for JSON APIs, plain text otherwise)"
        },
        "timeout": {
            "type": "integer",
            "description": "Request timeout in seconds (default 30)"
        }
    }
}
```

**Implementation notes:**
- Most flexible tool but least opinionated — the agent gets raw responses
- URL allowlist/denylist enforced via shared HttpClient (prevent SSRF)
- Response includes: `{status_code, headers, body, elapsed_ms}`
- Body size limit on response (shared with HttpClient, default 1MB)
- Auth header injection: configured API keys can be auto-injected by the kernel for known services

### `web_search`

Search the web via a configured search API.

```json
{
    "name": "web_search",
    "description": "Search the web for information. Returns titles, URLs, and snippets for the top results.",
    "parameters": {
        "query": {
            "type": "string",
            "description": "Search query string"
        },
        "count": {
            "type": "integer",
            "description": "Number of results to return (1-10, default 5)"
        },
        "freshness": {
            "type": "string",
            "enum": ["day", "week", "month", "year"],
            "description": "Filter results by recency. Omit for no time filter."
        }
    }
}
```

**Implementation notes:**
- Provider-agnostic: Brave Search, Google Custom Search, SearXNG, etc. — configured in kernel config
- Returns structured results: `[{title, url, snippet, published_date}]`
- Rate limiting and cost tracking per-provider
- The agent decides whether to use `web_search` or `web_fetch` based on the task — search for "find me information about X", fetch for "get me the content of this URL"

### `web_fetch`

Fetch and extract readable content from a URL.

```json
{
    "name": "web_fetch",
    "description": "Fetch a URL and extract its readable content as text or markdown. Use this to read web pages, API documentation, or any URL.",
    "parameters": {
        "url": {
            "type": "string",
            "description": "URL to fetch"
        },
        "format": {
            "type": "string",
            "enum": ["markdown", "text", "raw"],
            "description": "Output format. 'markdown' preserves structure, 'text' strips formatting, 'raw' returns the response body as-is (for APIs)."
        },
        "max_length": {
            "type": "integer",
            "description": "Maximum characters to return (default 50000). Truncates beyond this."
        }
    }
}
```

**Implementation notes:**
- Content extraction: HTML → markdown/text conversion (readability + markdown converter)
- `raw` mode for API responses, JSON, etc. — no conversion, just the body
- Respects robots.txt and rate limits
- Redirect following with max-redirect limit (default 5)
- Content-Type detection for format auto-selection
- Caching: short TTL cache for repeated fetches (configurable, default 5 min)

## Search Provider Configuration

Search providers are configured in the kernel config:

```cpp
struct SearchConfig {
    std::string provider;  // "brave", "google", "searxng"
    std::string api_key;
    std::string endpoint;  // For self-hosted (SearXNG)
    int default_count{5};
    int rate_limit_per_minute{10};
};
```

## Acceptance Criteria

- [ ] Shared HttpClient implemented with connection pooling and timeout support
- [ ] URL allowlist/denylist enforced on all outbound requests
- [ ] Response body size limit enforced (default 1MB)
- [ ] Redirect following with max-redirect limit (default 5)
- [ ] `http` supports GET, POST, PUT, PATCH, DELETE, HEAD methods
- [ ] `http` returns structured response with status, headers, body, elapsed_ms
- [ ] `http` enforces URL allowlist/denylist (SSRF prevention)
- [ ] `web_search` returns structured results (title, url, snippet, published_date)
- [ ] `web_search` supports freshness filtering
- [ ] `web_search` provider-agnostic (at least Brave Search implemented)
- [ ] `web_fetch` extracts readable content from HTML as markdown or text
- [ ] `web_fetch` raw mode returns unprocessed response body
- [ ] `web_fetch` respects max_length truncation
- [ ] `web_fetch` Content-Type detection for format auto-selection
- [ ] Short TTL response cache for web_fetch (default 5 min)
- [ ] All tools registered in ToolRegistry and wired through ChainRunner tool loop
- [ ] Integration test: each tool callable through the chain execution pipeline

## Notes

- `web_fetch` vs `http` distinction: fetch is for reading, http is for interacting. Fetch converts HTML to text; http gives you the raw response. The agent should use `web_fetch` when it wants to *read a webpage* and `http` when it wants to *call an API*.
- SSRF prevention for `http` is critical — the agent should not be able to hit internal services unless explicitly allowed.
- The three tools share a single HTTP client instance — no duplicate infrastructure.
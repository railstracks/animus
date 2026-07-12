#pragma once

#include "animus_kernel/tools/HttpClient.h"
#include "animus_kernel/tools/ToolRegistry.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace animus::kernel {

// ============================================================================
// Search Types
// ============================================================================

struct SearchQuery {
    std::string query;
    int count{5};
    std::string freshness;   // "day", "week", "month", "year", or empty
    std::string country;     // ISO 3166-1 alpha-2, or empty
    std::string language;    // ISO 639-1, or empty
};

struct SearchResult {
    std::string title;
    std::string url;
    std::string snippet;
    std::string published_date;
};

// ============================================================================
// ISearchProvider — interface for search backends
// ============================================================================

class ISearchProvider {
public:
    virtual ~ISearchProvider() = default;
    virtual std::string Name() const = 0;
    virtual std::vector<SearchResult> Search(const SearchQuery& query,
                                              std::string& error) = 0;
};

// ============================================================================
// BraveSearchProvider — Brave Search API
// ============================================================================

class BraveSearchProvider : public ISearchProvider {
public:
    struct Config {
        std::string api_key;
        int rate_limit_per_minute{10};
    };

    explicit BraveSearchProvider(HttpClient& client, Config config);

    std::string Name() const override { return "brave"; }
    std::vector<SearchResult> Search(const SearchQuery& query,
                                      std::string& error) override;

private:
    HttpClient& m_client;
    Config m_config;
    std::mutex m_rateMutex;
    std::vector<std::chrono::steady_clock::time_point> m_requestTimes;

    bool CheckRateLimit();
    void RecordRequest();
    static std::string FreshnessToBrave(const std::string& freshness);
};

// ============================================================================
// WebSearchTool — tool handler for web_search
// ============================================================================

class WebSearchTool : public IToolHandler {
public:
    WebSearchTool(std::unique_ptr<ISearchProvider> provider,
                  int defaultCount = 5,
                  int maxCount = 20);

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

private:
    std::unique_ptr<ISearchProvider> m_provider;
    int m_defaultCount;
    int m_maxCount;
};

} // namespace animus::kernel

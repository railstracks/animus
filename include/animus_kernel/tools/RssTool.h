#pragma once

#include "animus_kernel/tools/HttpClient.h"
#include "animus_kernel/tools/ToolRegistry.h"

#include <string>
#include <vector>

namespace animus::kernel {

// ============================================================================
// RssTool — fetch and parse RSS/Atom feeds from pre-configured sources
//
// Operator configures named feeds (id, label, url) in KernelConfig.
// Agent calls rss() for all feeds, rss("market-news") for one feed,
// with optional limit parameter.
// ============================================================================

class RssTool : public IToolHandler {
public:
    struct RssFeed {
        std::string id;
        std::string label;
        std::string url;
    };

    /// @param client Shared HTTP client instance. Must outlive this tool.
    /// @param feeds Pre-configured RSS feeds from KernelConfig.
    explicit RssTool(HttpClient& client, std::vector<RssFeed> feeds);

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

private:
    HttpClient& m_client;
    std::vector<RssFeed> m_feeds;

    struct FeedItem {
        std::string title;
        std::string link;
        std::string pub_date;
        std::string description;
        std::string feed_id;
        std::string feed_label;
    };

    /// Parse RSS 2.0 or Atom 1.0 XML into feed items.
    std::vector<FeedItem> ParseFeed(const std::string& xml,
                                     const std::string& feedId,
                                     const std::string& feedLabel) const;

    /// Fetch a single feed, returning items or empty on failure.
    std::vector<FeedItem> FetchFeed(const RssFeed& feed, int timeoutSeconds) const;

    /// Truncate description to maxLen characters, adding "..." if truncated.
    static std::string Truncate(const std::string& text, std::size_t maxLen);

    /// Strip HTML tags from a string.
    static std::string StripHtml(const std::string& html);
};

} // namespace animus::kernel
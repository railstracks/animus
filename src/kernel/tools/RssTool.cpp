#include "animus_kernel/tools/RssTool.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include <json/json.h>

namespace animus::kernel {

namespace {

Json::Value ParseArgs(const std::string& args) {
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::istringstream stream(args);
    std::string errors;
    if (!Json::parseFromStream(builder, stream, &root, &errors)) {
        return {};
    }
    return root;
}

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Minimal XML tag extraction — no external dependency.
// Extracts tag content from simple XML structures used in RSS/Atom.
struct XmlParser {
    const std::string& xml;
    std::size_t pos{0};

    explicit XmlParser(const std::string& s) : xml(s) {}

    // Find next occurrence of tag (opening or closing).
    // Returns position or std::string::npos.
    std::size_t FindTag(const std::string& tag, bool closing, std::size_t from) {
        std::string needle = closing ? "</" + tag : "<" + tag;
        std::size_t p = xml.find(needle, from);
        if (p == std::string::npos) return p;
        // Ensure it's a proper tag end (> or space/attribute)
        std::size_t end = xml.find('>', p);
        if (end == std::string::npos) return std::string::npos;
        return p;
    }

    // Extract text content between <tag...> and </tag>.
    // Handles attributes in opening tag. Returns empty if not found.
    std::string GetTagContent(const std::string& tag, std::size_t from, std::size_t searchEnd) {
        std::string openNeedle = "<" + tag;
        std::size_t openStart = xml.find(openNeedle, from);
        if (openStart == std::string::npos || openStart >= searchEnd) return "";

        // Skip past the opening tag (may have attributes)
        std::size_t openEnd = xml.find('>', openStart);
        if (openEnd == std::string::npos) return "";
        openEnd++; // past the '>'

        // Handle self-closing tags
        if (xml[openEnd - 2] == '/') return "";

        // Check for CDATA
        if (openEnd + 8 < xml.size() && xml.substr(openEnd, 9) == "<![CDATA[") {
            std::size_t cdataEnd = xml.find("]]>", openEnd + 9);
            if (cdataEnd == std::string::npos) return "";
            std::string content = xml.substr(openEnd + 9, cdataEnd - openEnd - 9);
            // Look for closing tag after CDATA
            return content;
        }

        // Find closing tag
        std::string closeNeedle = "</" + tag + ">";
        std::size_t closeStart = xml.find(closeNeedle, openEnd);
        if (closeStart == std::string::npos || closeStart >= searchEnd) return "";

        return xml.substr(openEnd, closeStart - openEnd);
    }

    // Find all occurrences of a repeating element and return [start, end) ranges
    // for each one's content (between opening and closing tags).
    std::vector<std::pair<std::size_t, std::size_t>>
    FindAllElements(const std::string& tag, std::size_t from, std::size_t searchEnd) {
        std::vector<std::pair<std::size_t, std::size_t>> result;
        std::string openNeedle = "<" + tag;
        std::size_t p = from;
        while (p < searchEnd) {
            std::size_t openStart = xml.find(openNeedle, p);
            if (openStart == std::string::npos || openStart >= searchEnd) break;

            std::size_t openEnd = xml.find('>', openStart);
            if (openEnd == std::string::npos) break;
            openEnd++;

            // Self-closing?
            if (xml[openEnd - 2] == '/') {
                p = openEnd;
                continue;
            }

            std::string closeNeedle = "</" + tag + ">";
            std::size_t closeStart = xml.find(closeNeedle, openEnd);
            if (closeStart == std::string::npos) break;

            result.push_back({openEnd, closeStart});
            p = closeStart + closeNeedle.size();
        }
        return result;
    }
};

} // namespace

RssTool::RssTool(HttpClient& client, std::vector<RssFeed> feeds)
    : m_client(client), m_feeds(std::move(feeds)) {}

ToolDefinition RssTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "rss";
    def.description =
        "Fetch RSS/Atom feeds. Returns itemized list of articles with "
        "title, link, date, and summary. Call without arguments to fetch "
        "all configured feeds.";
    def.resultMode = ToolResultMode::deliver_to_model;
    def.session_types = {"default"};

    ToolParameter feedParam;
    feedParam.name = "feed";
    feedParam.type = "string";
    feedParam.description = "Feed identifier to fetch. Omit or use \"all\" for all configured feeds.";
    feedParam.required = false;
    def.parameters.push_back(feedParam);

    ToolParameter limitParam;
    limitParam.name = "limit";
    limitParam.type = "integer";
    limitParam.description = "Maximum items per feed (default: 10).";
    limitParam.required = false;
    def.parameters.push_back(limitParam);

    // Config parameters (per-agent, stored in tool_configs.rss_feeds)
    ToolParameter cfgFeeds;
    cfgFeeds.name = "feeds";
    cfgFeeds.type = "array";
    cfgFeeds.description = "Array of RSS feed objects: {id, label, url}. Per-agent override for system-wide defaults.";
    cfgFeeds.required = false;
    def.config_parameters.push_back(cfgFeeds);

    return def;
}

std::string RssTool::Truncate(const std::string& text, std::size_t maxLen) {
    if (text.size() <= maxLen) return text;
    return text.substr(0, maxLen) + "...";
}

std::string RssTool::StripHtml(const std::string& html) {
    std::string result;
    bool inTag = false;
    for (char c : html) {
        if (c == '<') { inTag = true; continue; }
        if (c == '>') { inTag = false; continue; }
        if (!inTag) result.push_back(c);
    }
    // Collapse whitespace
    std::string collapsed;
    bool prevSpace = false;
    for (char c : result) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!prevSpace) { collapsed.push_back(' '); prevSpace = true; }
        } else {
            collapsed.push_back(c);
            prevSpace = false;
        }
    }
    return collapsed;
}

std::vector<RssTool::FeedItem> RssTool::ParseFeed(const std::string& xml,
                                                    const std::string& feedId,
                                                    const std::string& feedLabel) const {
    std::vector<FeedItem> items;
    XmlParser parser(xml);

    // Detect RSS 2.0 vs Atom 1.0
    bool isAtom = xml.find("<feed") != std::string::npos &&
                  xml.find("<feed xmlns=\"http://www.w3.org/2005/Atom\"") != std::string::npos;
    // Fallback check for Atom without namespace prefix
    if (!isAtom) {
        isAtom = xml.find("<feed") != std::string::npos && xml.find("<entry") != std::string::npos;
    }

    if (isAtom) {
        // Atom 1.0: entries
        auto entries = parser.FindAllElements("entry", 0, xml.size());
        for (const auto& [start, end] : entries) {
            FeedItem item;
            item.feed_id = feedId;
            item.feed_label = feedLabel;

            item.title = StripHtml(parser.GetTagContent("title", start, end));

            // Atom link: <link href="..."/> — extract href attribute
            std::string linkNeedle = "<link";
            std::size_t linkPos = xml.find(linkNeedle, start);
            if (linkPos != std::string::npos && linkPos < end) {
                std::size_t hrefPos = xml.find("href=\"", linkPos);
                if (hrefPos != std::string::npos && hrefPos < end) {
                    hrefPos += 6;
                    std::size_t hrefEnd = xml.find('"', hrefPos);
                    if (hrefEnd != std::string::npos && hrefEnd <= end) {
                        item.link = xml.substr(hrefPos, hrefEnd - hrefPos);
                    }
                }
            }

            item.pub_date = parser.GetTagContent("published", start, end);
            if (item.pub_date.empty()) {
                item.pub_date = parser.GetTagContent("updated", start, end);
            }

            std::string summary = parser.GetTagContent("summary", start, end);
            if (summary.empty()) {
                summary = parser.GetTagContent("content", start, end);
            }
            item.description = Truncate(StripHtml(summary), 500);

            items.push_back(std::move(item));
        }
    } else {
        // RSS 2.0: items inside <channel>
        auto itemRanges = parser.FindAllElements("item", 0, xml.size());
        for (const auto& [start, end] : itemRanges) {
            FeedItem item;
            item.feed_id = feedId;
            item.feed_label = feedLabel;

            item.title = StripHtml(parser.GetTagContent("title", start, end));
            item.link = parser.GetTagContent("link", start, end);
            item.pub_date = parser.GetTagContent("pubDate", start, end);
            if (item.pub_date.empty()) {
                item.pub_date = parser.GetTagContent("pubdate", start, end);
            }

            std::string desc = parser.GetTagContent("description", start, end);
            item.description = Truncate(StripHtml(desc), 500);

            items.push_back(std::move(item));
        }
    }

    return items;
}

std::vector<RssTool::FeedItem> RssTool::FetchFeed(const RssFeed& feed, int timeoutSeconds) const {
    HttpClient::Request req;
    req.url = feed.url;
    req.method = "GET";
    req.timeout_seconds = timeoutSeconds;

    auto resp = m_client.Execute(req);
    if (resp.status_code == 0 || resp.status_code >= 400) {
        return {};
    }

    return ParseFeed(resp.body, feed.id, feed.label);
}

ToolResult RssTool::Execute(const ToolCall& call) {
    ToolResult result;
    result.call_id = call.id;

    // Determine which feeds to use: per-agent injected config overrides constructor defaults
    std::vector<RssFeed> activeFeeds = m_feeds;
    const auto args = ParseArgs(call.arguments);

    if (args.isMember("__config") && args["__config"].isObject() &&
        args["__config"].isMember("feeds") && args["__config"]["feeds"].isArray()) {
        activeFeeds.clear();
        for (const auto& item : args["__config"]["feeds"]) {
            RssFeed feed;
            feed.id = item.get("id", "").asString();
            feed.label = item.get("label", "").asString();
            feed.url = item.get("url", "").asString();
            if (!feed.id.empty() && !feed.url.empty()) {
                activeFeeds.push_back(std::move(feed));
            }
        }
    }

    if (activeFeeds.empty()) {
        result.success = false;
        result.error = "No RSS feeds are configured for this agent. Add feeds in the tool configuration.";
        return result;
    }

    std::string feedId;
    if (args.isMember("feed") && args["feed"].isString()) {
        feedId = args["feed"].asString();
    }

    int limit = 10;
    if (args.isMember("limit") && args["limit"].isInt()) {
        limit = args["limit"].asInt();
        if (limit <= 0) limit = 10;
        if (limit > 50) limit = 50;
    }

    // Determine which feeds to fetch
    std::vector<const RssFeed*> toFetch;
    if (feedId.empty() || ToLower(feedId) == "all") {
        for (const auto& f : activeFeeds) toFetch.push_back(&f);
    } else {
        auto it = std::find_if(activeFeeds.begin(), activeFeeds.end(),
            [&](const RssFeed& f) { return f.id == feedId; });
        if (it == activeFeeds.end()) {
            result.success = false;
            result.error = "No RSS feed with id \"" + feedId + "\".";
            return result;
        }
        toFetch.push_back(&(*it));
    }

    // Fetch feeds
    std::vector<FeedItem> allItems;
    std::vector<std::string> failedFeeds;

    for (const auto* feed : toFetch) {
        auto items = FetchFeed(*feed, 10);
        if (items.empty()) {
            failedFeeds.push_back(feed->id);
        } else {
            // Apply limit
            if (static_cast<int>(items.size()) > limit) {
                items.resize(limit);
            }
            allItems.insert(allItems.end(), items.begin(), items.end());
        }
    }

    // Sort by pub_date descending (RSS dates are typically RFC 822)
    std::sort(allItems.begin(), allItems.end(),
        [](const FeedItem& a, const FeedItem& b) {
            return a.pub_date > b.pub_date;
        });

    // Build JSON output
    Json::Value root(Json::objectValue);
    root["total_items"] = static_cast<int>(allItems.size());

    Json::Value itemsArray(Json::arrayValue);
    for (const auto& item : allItems) {
        Json::Value entry(Json::objectValue);
        entry["title"] = item.title;
        entry["link"] = item.link;
        entry["pub_date"] = item.pub_date;
        entry["description"] = item.description;
        entry["feed_id"] = item.feed_id;
        entry["feed_label"] = item.feed_label;
        itemsArray.append(entry);
    }
    root["items"] = itemsArray;

    if (!failedFeeds.empty()) {
        Json::Value failed(Json::arrayValue);
        for (const auto& f : failedFeeds) failed.append(f);
        root["failed_feeds"] = failed;
    }

    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    result.success = true;
    result.output = Json::writeString(wb, root);
    return result;
}

} // namespace animus::kernel
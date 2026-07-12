#include "animus_kernel/tools/WebSearchTool.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

#include <json/json.h>

namespace animus::kernel {

namespace {

// Minimal URL-encoding for query parameters.
std::string UrlEncode(const std::string& s) {
    std::ostringstream out;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '~' || c == ' ') {
            if (c == ' ') {
                out << '+';
            } else {
                out << static_cast<char>(c);
            }
        } else {
            out << '%' << std::uppercase << std::setw(2) << std::setfill('0')
                << std::hex << static_cast<int>(c);
        }
    }
    return out.str();
}

} // namespace

// ============================================================================
// BraveSearchProvider
// ============================================================================

BraveSearchProvider::BraveSearchProvider(HttpClient& client, Config config)
    : m_client(client), m_config(std::move(config)) {}

std::string BraveSearchProvider::FreshnessToBrave(const std::string& freshness) {
    if (freshness == "day")   return "pd";   // past day
    if (freshness == "week")  return "pw";   // past week
    if (freshness == "month") return "pm";   // past month
    if (freshness == "year")  return "py";   // past year
    return {};
}

bool BraveSearchProvider::CheckRateLimit() {
    std::lock_guard<std::mutex> lock(m_rateMutex);
    auto now = std::chrono::steady_clock::now();
    auto windowStart = now - std::chrono::minutes(1);

    // Erase timestamps older than 1 minute.
    m_requestTimes.erase(
        std::remove_if(m_requestTimes.begin(), m_requestTimes.end(),
                        [&](const auto& tp) { return tp < windowStart; }),
        m_requestTimes.end());

    return static_cast<int>(m_requestTimes.size()) < m_config.rate_limit_per_minute;
}

void BraveSearchProvider::RecordRequest() {
    std::lock_guard<std::mutex> lock(m_rateMutex);
    m_requestTimes.push_back(std::chrono::steady_clock::now());
}

std::vector<SearchResult> BraveSearchProvider::Search(const SearchQuery& query,
                                                        std::string& error) {
    error.clear();

    if (m_config.api_key.empty()) {
        error = "Brave Search API key is not configured";
        return {};
    }

    if (!CheckRateLimit()) {
        error = "Rate limit exceeded (" + std::to_string(m_config.rate_limit_per_minute)
                + " requests/minute)";
        return {};
    }

    // Build URL
    std::ostringstream url;
    url << "https://api.search.brave.com/res/v1/web/search?q="
        << UrlEncode(query.query);

    int count = std::clamp(query.count, 1, 20);
    url << "&count=" << count;

    if (!query.freshness.empty()) {
        auto braveFresh = FreshnessToBrave(query.freshness);
        if (!braveFresh.empty()) {
            url << "&freshness=" << braveFresh;
        }
    }
    if (!query.country.empty()) {
        url << "&country=" << query.country;
    }
    if (!query.language.empty()) {
        url << "&search_lang=" << query.language;
    }

    HttpClient::Request req;
    req.method = "GET";
    req.url = url.str();
    req.headers["Accept"] = "application/json";
    req.headers["X-Subscription-Token"] = m_config.api_key;
    req.timeout_seconds = 15;

    auto resp = m_client.Execute(req);
    RecordRequest();

    if (resp.status_code == 429) {
        error = "Brave API rate limit hit (HTTP 429)";
        return {};
    }
    if (resp.status_code == 401 || resp.status_code == 403) {
        error = "Brave API authentication failed (HTTP " + std::to_string(resp.status_code) + ")";
        return {};
    }
    if (resp.status_code != 200) {
        error = "Brave API returned HTTP " + std::to_string(resp.status_code);
        if (!resp.error.empty()) error += ": " + resp.error;
        return {};
    }

    // Parse JSON response
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::istringstream stream(resp.body);
    std::string parseErrors;
    if (!Json::parseFromStream(builder, stream, &root, &parseErrors)) {
        error = "Failed to parse Brave response: " + parseErrors;
        return {};
    }

    std::vector<SearchResult> results;

    // Brave web.search results are in "web" → "results" array
    const Json::Value* webResults = nullptr;
    if (root.isMember("web") && root["web"].isMember("results")) {
        webResults = &root["web"]["results"];
    } else if (root.isMember("results")) {
        // Some response shapes have results at top level
        webResults = &root["results"];
    }

    if (webResults && webResults->isArray()) {
        for (const auto& item : *webResults) {
            SearchResult result;
            result.title = item.isMember("title") ? item["title"].asString() : "";
            result.url = item.isMember("url") ? item["url"].asString()
                        : (item.isMember("link") ? item["link"].asString() : "");
            result.snippet = item.isMember("description")
                                ? item["description"].asString()
                                : (item.isMember("snippet") ? item["snippet"].asString() : "");
            result.published_date = item.isMember("age")
                                        ? item["age"].asString()
                                        : (item.isMember("published") ? item["published"].asString() : "");
            if (!result.url.empty()) {
                results.push_back(std::move(result));
            }
        }
    }

    return results;
}

// ============================================================================
// WebSearchTool
// ============================================================================

WebSearchTool::WebSearchTool(std::unique_ptr<ISearchProvider> provider,
                              int defaultCount,
                              int maxCount)
    : m_provider(std::move(provider)),
      m_defaultCount(defaultCount),
      m_maxCount(maxCount) {}

ToolDefinition WebSearchTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "web_search";
    def.description =
        "Search the web for current information. Returns titles, URLs, and snippets "
        "for the top results. Use web_search for finding information, web_fetch for "
        "reading a specific URL.";
    def.resultMode = ToolResultMode::deliver_to_model;

    ToolParameter queryParam;
    queryParam.name = "query";
    queryParam.type = "string";
    queryParam.description = "Search query string";
    queryParam.required = true;
    def.parameters.push_back(queryParam);

    ToolParameter countParam;
    countParam.name = "count";
    countParam.type = "integer";
    countParam.description = "Number of results (1-" + std::to_string(m_maxCount)
                              + ", default " + std::to_string(m_defaultCount) + ")";
    countParam.default_value_json = std::to_string(m_defaultCount);
    def.parameters.push_back(countParam);

    ToolParameter freshnessParam;
    freshnessParam.name = "freshness";
    freshnessParam.type = "string";
    freshnessParam.description = "Filter results by recency. Omit for no time filter.";
    freshnessParam.enum_values = {"day", "week", "month", "year"};
    def.parameters.push_back(freshnessParam);

    return def;
}

ToolResult WebSearchTool::Execute(const ToolCall& call) {
    ToolResult result;
    result.call_id = call.id;

    // Parse arguments
    Json::Value args;
    Json::CharReaderBuilder builder;
    std::istringstream argStream(call.arguments);
    std::string parseErrors;
    if (!Json::parseFromStream(builder, argStream, &args, &parseErrors)) {
        result.success = false;
        result.error = "Failed to parse arguments: " + parseErrors;
        return result;
    }

    SearchQuery query;

    if (args.isMember("query") && args["query"].isString()) {
        query.query = args["query"].asString();
    }
    if (query.query.empty()) {
        result.success = false;
        result.error = "Missing required parameter: query";
        return result;
    }

    if (args.isMember("count") && args["count"].isInt()) {
        query.count = args["count"].asInt();
    } else {
        query.count = m_defaultCount;
    }
    query.count = std::clamp(query.count, 1, m_maxCount);

    if (args.isMember("freshness") && args["freshness"].isString()) {
        query.freshness = args["freshness"].asString();
    }

    std::string searchError;
    auto searchResults = m_provider->Search(query, searchError);

    if (!searchError.empty()) {
        result.success = false;
        result.error = searchError;
        return result;
    }

    // Build output JSON
    Json::Value output(Json::objectValue);
    output["query"] = query.query;
    output["count"] = static_cast<Json::UInt>(searchResults.size());

    Json::Value resultsArray(Json::arrayValue);
    for (const auto& sr : searchResults) {
        Json::Value item(Json::objectValue);
        item["title"] = sr.title;
        item["url"] = sr.url;
        item["snippet"] = sr.snippet;
        if (!sr.published_date.empty()) {
            item["published_date"] = sr.published_date;
        }
        resultsArray.append(item);
    }
    output["results"] = resultsArray;

    Json::StreamWriterBuilder writer;
    result.output = Json::writeString(writer, output);

    result.success = true;
    return result;
}

} // namespace animus::kernel

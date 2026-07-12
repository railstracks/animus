#include "animus_kernel/tools/WebFetchTool.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

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

std::string GetStringField(const Json::Value& args, const std::string& key,
                           const std::string& defaultVal = "") {
    if (args.isMember(key) && args[key].isString()) {
        return args[key].asString();
    }
    return defaultVal;
}

int GetIntField(const Json::Value& args, const std::string& key, int defaultVal = 0) {
    if (args.isMember(key) && args[key].isInt()) {
        return args[key].asInt();
    }
    return defaultVal;
}

constexpr int kDefaultMaxLength = 50000;
constexpr std::size_t kMaxConvertibleHtmlBytes = 2U * 1024U * 1024U; // 2 MiB safety guard

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string DecodeHtmlEntities(const std::string& text) {
    std::string out;
    out.reserve(text.size());

    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            out.push_back(text[i]);
            continue;
        }

        const std::size_t semi = text.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 12) {
            out.push_back(text[i]);
            continue;
        }

        const std::string entity = text.substr(i + 1, semi - i - 1);
        if (entity == "amp") out.push_back('&');
        else if (entity == "lt") out.push_back('<');
        else if (entity == "gt") out.push_back('>');
        else if (entity == "quot") out.push_back('"');
        else if (entity == "apos" || entity == "#39") out.push_back('\'');
        else if (entity == "nbsp") out.push_back(' ');
        else if (!entity.empty() && entity[0] == '#') {
            // Numeric entities: decimal (e.g. &#60;) and hex (e.g. &#x3C;).
            char* end = nullptr;
            long code = 0;
            if (entity.size() > 2 && (entity[1] == 'x' || entity[1] == 'X')) {
                code = std::strtol(entity.c_str() + 2, &end, 16);
            } else {
                code = std::strtol(entity.c_str() + 1, &end, 10);
            }
            if (end && *end == '\0' && code > 0 && code <= 0x10FFFFL) {
                if (code <= 0x7FL) {
                    out.push_back(static_cast<char>(code));
                } else {
                    // Keep non-ASCII numeric entities stable and readable.
                    out.append("&#");
                    out.append(std::to_string(code));
                    out.push_back(';');
                }
            } else {
                out.push_back('&');
                out.append(entity);
                out.push_back(';');
            }
        } else {
            out.push_back('&');
            out.append(entity);
            out.push_back(';');
        }

        i = semi;
    }

    return out;
}

void AppendNewline(std::string* out, int count = 1) {
    if (!out || count <= 0) return;
    for (int i = 0; i < count; ++i) out->push_back('\n');
}

std::string NormalizeWhitespace(const std::string& input) {
    std::string out;
    out.reserve(input.size());

    int newlineRun = 0;
    bool lastWasSpace = false;
    for (char ch : input) {
        if (ch == '\r') continue;
        if (ch == '\n') {
            ++newlineRun;
            if (newlineRun <= 2) out.push_back('\n');
            lastWasSpace = false;
            continue;
        }

        newlineRun = 0;
        if (ch == '\t') ch = ' ';
        if (ch == ' ') {
            if (lastWasSpace) continue;
            lastWasSpace = true;
            out.push_back(ch);
            continue;
        }

        lastWasSpace = false;
        out.push_back(ch);
    }

    // Trim ends.
    while (!out.empty() && (out.front() == '\n' || out.front() == ' ' || out.front() == '\t')) {
        out.erase(out.begin());
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ' || out.back() == '\t')) {
        out.pop_back();
    }
    return out;
}

std::string TagNameLower(const std::string& rawTag) {
    std::size_t i = 0;
    while (i < rawTag.size() && std::isspace(static_cast<unsigned char>(rawTag[i]))) ++i;
    if (i < rawTag.size() && rawTag[i] == '/') ++i;
    while (i < rawTag.size() && std::isspace(static_cast<unsigned char>(rawTag[i]))) ++i;

    const std::size_t start = i;
    while (i < rawTag.size()) {
        const unsigned char c = static_cast<unsigned char>(rawTag[i]);
        if (std::isalnum(c) || c == '-' || c == '_') {
            ++i;
            continue;
        }
        break;
    }
    return ToLowerAscii(rawTag.substr(start, i - start));
}

bool IsClosingTag(const std::string& rawTag) {
    std::size_t i = 0;
    while (i < rawTag.size() && std::isspace(static_cast<unsigned char>(rawTag[i]))) ++i;
    return i < rawTag.size() && rawTag[i] == '/';
}

std::string ConvertHtmlWithoutRegex(const std::string& html, bool markdown) {
    std::string out;
    out.reserve(html.size());

    bool inScript = false;
    bool inStyle = false;
    bool inPre = false;

    std::size_t i = 0;
    while (i < html.size()) {
        if (html[i] != '<') {
            if (!inScript && !inStyle) out.push_back(html[i]);
            ++i;
            continue;
        }

        // HTML comments.
        if (i + 3 < html.size() && html.compare(i, 4, "<!--") == 0) {
            const std::size_t commentEnd = html.find("-->", i + 4);
            if (commentEnd == std::string::npos) break;
            i = commentEnd + 3;
            continue;
        }

        const std::size_t tagEnd = html.find('>', i + 1);
        if (tagEnd == std::string::npos) {
            if (!inScript && !inStyle) out.push_back(html[i]);
            ++i;
            continue;
        }

        const std::string rawTag = html.substr(i + 1, tagEnd - i - 1);
        const std::string name = TagNameLower(rawTag);
        const bool closing = IsClosingTag(rawTag);

        i = tagEnd + 1;
        if (name.empty()) continue;

        if (name == "script") {
            inScript = !closing;
            continue;
        }
        if (name == "style") {
            inStyle = !closing;
            continue;
        }
        if (inScript || inStyle) continue;

        if (markdown) {
            if (name == "pre") {
                AppendNewline(&out);
                out.append("```");
                AppendNewline(&out);
                inPre = !closing;
                continue;
            }
            if (name == "code" && !inPre) {
                out.push_back('`');
                continue;
            }
            if (name == "br" || name == "hr") {
                AppendNewline(&out);
                continue;
            }
            if (name == "p" || name == "div") {
                if (closing) AppendNewline(&out, 2);
                continue;
            }
            if (name == "li" && !closing) {
                out.append("\n- ");
                continue;
            }
            if ((name == "ul" || name == "ol") && closing) {
                AppendNewline(&out);
                continue;
            }
            if (!closing && name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') {
                AppendNewline(&out);
                out.append(static_cast<std::size_t>(name[1] - '0'), '#');
                out.push_back(' ');
                continue;
            }
            if (closing && name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') {
                AppendNewline(&out);
                continue;
            }
            if (name == "strong" || name == "b") {
                out.append("**");
                continue;
            }
            if (name == "em" || name == "i") {
                out.push_back('*');
                continue;
            }
        } else {
            if (name == "br" || name == "hr" || name == "p" || name == "div" || name == "li"
                || name == "tr" || name == "td") {
                AppendNewline(&out);
                continue;
            }
            if (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') {
                AppendNewline(&out);
                continue;
            }
        }
    }

    return NormalizeWhitespace(DecodeHtmlEntities(out));
}

} // anonymous namespace

// ============================================================================
// Construction
// ============================================================================

WebFetchTool::WebFetchTool(HttpClient& client)
    : m_client(client) {}

// ============================================================================
// Definition
// ============================================================================

ToolDefinition WebFetchTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "web_fetch";
    def.description =
        "Fetch a URL and extract its readable content as text or markdown. "
        "Use this to read web pages, API documentation, or any URL. "
        "Use 'http' for raw API calls and 'web_search' to find information.";
    def.resultMode = ToolResultMode::deliver_to_model;

    ToolParameter urlParam;
    urlParam.name = "url";
    urlParam.type = "string";
    urlParam.description = "URL to fetch";
    urlParam.required = true;
    def.parameters.push_back(urlParam);

    ToolParameter formatParam;
    formatParam.name = "format";
    formatParam.type = "string";
    formatParam.description =
        "Output format. 'markdown' preserves structure, 'text' strips formatting, "
        "'raw' returns the response body as-is (for APIs, JSON, etc.).";
    formatParam.required = false;
    formatParam.enum_values = {"markdown", "text", "raw"};
    def.parameters.push_back(formatParam);

    ToolParameter maxLengthParam;
    maxLengthParam.name = "max_length";
    maxLengthParam.type = "integer";
    maxLengthParam.description = "Maximum characters to return (default 50000). Truncates beyond this.";
    maxLengthParam.required = false;
    def.parameters.push_back(maxLengthParam);

    return def;
}

// ============================================================================
// Execute
// ============================================================================

ToolResult WebFetchTool::Execute(const ToolCall& call) {
    ToolResult result;
    result.call_id = call.id;

    auto args = ParseArgs(call.arguments);
    if (args.isNull()) {
        result.success = false;
        result.error = "Failed to parse tool arguments as JSON";
        return result;
    }

    std::string url = GetStringField(args, "url");
    if (url.empty()) {
        result.success = false;
        result.error = "Missing required parameter: url";
        return result;
    }

    std::string requestedFormat = GetStringField(args, "format", "markdown");
    int maxLength = GetIntField(args, "max_length", kDefaultMaxLength);
    if (maxLength <= 0) maxLength = kDefaultMaxLength;

    // Build request
    HttpClient::Request req;
    req.method = "GET";
    req.url = url;
    req.timeout_seconds = 30;

    // Execute
    auto response = m_client.Execute(req);

    if (!response.error.empty()) {
        result.success = false;
        result.error = response.error;
        return result;
    }

    if (response.status_code < 200 || response.status_code >= 400) {
        result.success = false;
        result.error = "HTTP " + std::to_string(response.status_code) +
                       " fetching " + url;
        return result;
    }

    // Detect format
    std::string effectiveFormat = DetectFormat(requestedFormat, response.content_type);

    // Convert content
    std::string content;
    if (effectiveFormat == "raw") {
        content = response.body;
    } else if (response.body.size() > kMaxConvertibleHtmlBytes) {
        // Guard against pathological inputs and heavy conversions.
        effectiveFormat = "raw";
        content = response.body;
    } else {
        content = (effectiveFormat == "text")
            ? HtmlToText(response.body)
            : HtmlToMarkdown(response.body);
    }

    // Truncate
    if (static_cast<int>(content.size()) > maxLength) {
        content.resize(maxLength);
        content += "\n\n... (truncated at " + std::to_string(maxLength) + " characters)";
    }

    // Build output
    Json::Value output(Json::objectValue);
    output["url"] = url;
    output["format"] = effectiveFormat;
    output["status_code"] = response.status_code;
    output["content_type"] = response.content_type;
    output["content"] = content;
    output["elapsed_ms"] = static_cast<Json::UInt64>(response.elapsed_ms);

    Json::StreamWriterBuilder writer;
    result.output = Json::writeString(writer, output);
    result.success = true;

    return result;
}

// ============================================================================
// Format detection
// ============================================================================

std::string WebFetchTool::DetectFormat(const std::string& requestedFormat,
                                        const std::string& contentType) const {
    // If user explicitly requested raw, honor it
    if (requestedFormat == "raw") return "raw";

    // If content is not HTML, use raw
    if (contentType.find("text/html") == std::string::npos &&
        contentType.find("application/xhtml") == std::string::npos) {
        // JSON, XML, plain text, binary — return as-is
        if (requestedFormat != "raw") {
            // For non-HTML content, raw is the sensible default
            return "raw";
        }
    }

    return requestedFormat.empty() ? "markdown" : requestedFormat;
}

// ============================================================================
// HTML to text — strip tags, normalize whitespace
// ============================================================================

std::string WebFetchTool::HtmlToText(const std::string& html) const {
    return ConvertHtmlWithoutRegex(html, false);
}

// ============================================================================
// HTML to markdown — simplified conversion
// ============================================================================

std::string WebFetchTool::HtmlToMarkdown(const std::string& html) const {
    return ConvertHtmlWithoutRegex(html, true);
}

} // namespace animus::kernel

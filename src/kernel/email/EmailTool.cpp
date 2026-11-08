#include "animus_kernel/tools/EmailTool.h"
#include "animus_kernel/AgentConfigStore.h"

#include <json/json.h>
#include <json/writer.h>

#include <sstream>
#include <iostream>
#include <map>

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

std::string GetString(const Json::Value& v, const std::string& key,
                       const std::string& def = "") {
    return (v.isMember(key) && v[key].isString()) ? v[key].asString() : def;
}

int GetInt(const Json::Value& v, const std::string& key, int def = 0) {
    return (v.isMember(key) && v[key].isInt()) ? v[key].asInt() : def;
}

bool GetBool(const Json::Value& v, const std::string& key, bool def = false) {
    return (v.isMember(key) && v[key].isBool()) ? v[key].asBool() : def;
}

std::string OkResult(const Json::Value& data) {
    Json::Value result(Json::objectValue);
    result["status"] = 200;
    result["data"] = data;
    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    return Json::writeString(wb, result);
}

std::string ErrResult(int code, const std::string& msg) {
    Json::Value result(Json::objectValue);
    result["status"] = code;
    result["error"] = msg;
    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    return Json::writeString(wb, result);
}

/// Build query string from key-value pairs.
std::string BuildQueryString(const std::map<std::string, std::string>& params) {
    if (params.empty()) return "";
    std::string q;
    bool first = true;
    for (const auto& [k, v] : params) {
        if (v.empty()) continue;
        if (!first) q += "&";
        q += k + "=" + v; // AgentMail API expects simple values, no URL-encode needed for our params
        first = false;
    }
    return q.empty() ? "" : "?" + q;
}

/// URL-encode characters that are invalid in URL path segments.
/// Specifically: <, >, space, ", `, #, %, {, }, |, \, ^, [, ], `
std::string UrlEncodePathSegment(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '<': out += "%3C"; break;
            case '>': out += "%3E"; break;
            case ' ': out += "%20"; break;
            case '"': out += "%22"; break;
            case '#': out += "%23"; break;
            case '%': out += "%25"; break;
            case '{': out += "%7B"; break;
            case '}': out += "%7D"; break;
            case '|': out += "%7C"; break;
            case '\\': out += "%5C"; break;
            case '^': out += "%5E"; break;
            case '[': out += "%5B"; break;
            case ']': out += "%5D"; break;
            case '`': out += "%60"; break;
            case '@': out += "%40"; break;
            default: out += c; break;
        }
    }
    return out;
}
/// AgentMail API base URL
static constexpr const char* kAgentMailBase = "https://api.agentmail.to";

} // anonymous namespace

// ============================================================================
// Construction
// ============================================================================

EmailTool::EmailTool(HttpClient& httpClient, AgentConfigStore* configStore)
    : m_httpClient(httpClient)
    , m_configStore(configStore)
{}

// ============================================================================
// Tool definition
// ============================================================================

ToolDefinition EmailTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "email";
    def.description =
        "Send, read, search, and manage email via AgentMail. "
        "List inboxes, browse threads and messages, reply, forward, "
        "manage drafts, and search across email. "
        "Use 'list_inboxes' to see configured accounts.";
    def.resultMode = ToolResultMode::deliver_to_model;

    // Available in default sessions and email:chat auto-reply sessions.
    // Excluded from consolidation sessions.
    def.session_types = {"default"};

    def.parameters.push_back({
        "action", "string",
        "Email operation to perform",
        true, "",
        {"list_inboxes", "list_threads", "get_thread", "get_message",
         "get_attachment", "list_attachments", "search", "send", "reply", "forward",
         "mark_read", "delete", "list_drafts", "create_draft", "send_draft"}
    });

    // Account selection
    def.parameters.push_back({
        "inbox_id", "string",
        "Inbox/account ID (optional — uses default if omitted)",
        false
    });

    // Thread/message parameters
    def.parameters.push_back({
        "thread_id", "string",
        "Thread ID (for get_thread, reply)",
        false
    });
    def.parameters.push_back({
        "message_id", "string",
        "Message ID (for get_message, reply, forward, mark_read, delete)",
        false
    });
    def.parameters.push_back({
        "attachment_id", "string",
        "Attachment ID (for get_attachment)",
        false
    });
    def.parameters.push_back({
        "attachments", "array",
        "Attachments to send (for send): [{filename: string, content: string}] "
        "where content is base64 of the file bytes. Server-side receipt is the "
        "thread view, not the send response.",
        false
    });

    // Send/reply/forward parameters
    def.parameters.push_back({
        "to", "string",
        "Recipient address (for send, reply, forward). Multiple addresses comma-separated. "
        "For reply: overrides the anchor-derived recipient; omit to reply to the "
        "original correspondent (resolved from the anchor's envelope).",
        false
    });
    def.parameters.push_back({
        "subject", "string",
        "Email subject (for send)",
        false
    });
    def.parameters.push_back({
        "body", "string",
        "Email body text (for send, reply, forward, create_draft)",
        false
    });
    def.parameters.push_back({
        "cc", "string",
        "CC recipients (comma-separated, optional for send)",
        false
    });
    def.parameters.push_back({
        "bcc", "string",
        "BCC recipients (comma-separated, optional for send)",
        false
    });

    // List/search parameters
    def.parameters.push_back({
        "limit", "integer",
        "Maximum results to return (default 20)",
        false
    });
    def.parameters.push_back({
        "page_token", "string",
        "Pagination token from previous response",
        false
    });
    def.parameters.push_back({
        "query", "string",
        "Search query (for search action)",
        false
    });
    def.parameters.push_back({
        "before", "string",
        "Filter: before this ISO timestamp",
        false
    });
    def.parameters.push_back({
        "after", "string",
        "Filter: after this ISO timestamp",
        false
    });

    // Draft parameters
    def.parameters.push_back({
        "draft_id", "string",
        "Draft ID (for send_draft)",
        false
    });

    return def;
}

// ============================================================================
// Execute
// ============================================================================

ToolResult EmailTool::Execute(const ToolCall& call) {
    ToolResult result;
    result.call_id = call.id;

    auto args = ParseArgs(call.arguments);
    if (args.isNull()) {
        result.success = false;
        result.error = "failed to parse arguments";
        return result;
    }

    std::string action = GetString(args, "action");
    if (action.empty()) {
        result.success = false;
        result.error = "action is required";
        return result;
    }

    if (action == "list_inboxes") {
        result.success = true;
        result.output = HandleListInboxes(args);
    } else if (action == "list_threads") {
        result.success = true;
        result.output = HandleListThreads(args);
    } else if (action == "get_thread") {
        result.success = true;
        result.output = HandleGetThread(args);
    } else if (action == "get_message") {
        result.success = true;
        result.output = HandleGetMessage(args);
    } else if (action == "get_attachment") {
        result.success = true;
        result.output = HandleGetAttachment(args);
    } else if (action == "list_attachments") {
        result.success = true;
        result.output = HandleListAttachments(args);
    } else if (action == "search") {
        result.success = true;
        result.output = HandleSearch(args);
    } else if (action == "send") {
        result.success = true;
        result.output = HandleSend(args);
    } else if (action == "reply") {
        result.success = true;
        result.output = HandleReply(args);
    } else if (action == "forward") {
        result.success = true;
        result.output = HandleForward(args);
    } else if (action == "mark_read") {
        result.success = true;
        result.output = HandleMarkRead(args);
    } else if (action == "delete") {
        result.success = true;
        result.output = HandleDelete(args);
    } else if (action == "list_drafts") {
        result.success = true;
        result.output = HandleListDrafts(args);
    } else if (action == "create_draft") {
        result.success = true;
        result.output = HandleCreateDraft(args);
    } else if (action == "send_draft") {
        result.success = true;
        result.output = HandleSendDraft(args);
    } else {
        result.success = false;
        result.error = "unknown action: " + action;
        return result;
    }

    // Check if output is an error
    if (result.output.find("\"error\"") != std::string::npos) {
        auto outJson = ParseArgs(result.output);
        if (outJson.isMember("error")) {
            result.success = false;
            result.error = outJson["error"].asString();
            result.output.clear();
        }
    }

    return result;
}

// ============================================================================
// AgentMail REST helpers
// ============================================================================

HttpClient::Response EmailTool::AgentMailGet(
        const std::string& inboxId,
        const std::string& path,
        const std::string& apiKey,
        const std::map<std::string, std::string>& query) {
    std::string url = std::string(kAgentMailBase) + path + BuildQueryString(query);
    HttpClient::Request req;
    req.method = "GET";
    req.url = url;
    req.headers["Authorization"] = "Bearer " + apiKey;
    req.headers["Content-Type"] = "application/json";
    return m_httpClient.Execute(req);
}

HttpClient::Response EmailTool::AgentMailPost(
        const std::string& /*inboxId*/,
        const std::string& path,
        const std::string& body,
        const std::string& apiKey) {
    std::string url = std::string(kAgentMailBase) + path;
    HttpClient::Request req;
    req.method = "POST";
    req.url = url;
    req.headers["Authorization"] = "Bearer " + apiKey;
    req.headers["Content-Type"] = "application/json";
    req.body = body;
    return m_httpClient.Execute(req);
}

HttpClient::Response EmailTool::AgentMailPut(
        const std::string& /*inboxId*/,
        const std::string& path,
        const std::string& body,
        const std::string& apiKey) {
    std::string url = std::string(kAgentMailBase) + path;
    HttpClient::Request req;
    req.method = "PUT";
    req.url = url;
    req.headers["Authorization"] = "Bearer " + apiKey;
    req.headers["Content-Type"] = "application/json";
    req.body = body;
    return m_httpClient.Execute(req);
}

HttpClient::Response EmailTool::AgentMailDelete(
        const std::string& /*inboxId*/,
        const std::string& path,
        const std::string& apiKey) {
    std::string url = std::string(kAgentMailBase) + path;
    HttpClient::Request req;
    req.method = "DELETE";
    req.url = url;
    req.headers["Authorization"] = "Bearer " + apiKey;
    return m_httpClient.Execute(req);
}


/// Minimal HTML-to-text stripper for email body fallback.
/// Removes tags, decodes common entities, collapses whitespace.
std::string StripHtml(const std::string& html) {
    std::string out;
    out.reserve(html.size());
    bool inTag = false;
    bool inEntity = false;
    std::string entity;
    bool lastWasSpace = false;

    for (size_t i = 0; i < html.size(); ++i) {
        char c = html[i];
        if (inTag) {
            if (c == '>') {
                inTag = false;
                // Add newline after block-level tags
                if (i + 1 <= html.size()) {
                    // Peek back at tag name
                    std::string tag;
                    for (int j = (int)out.size() - 1; j >= 0 && out[j] != '<'; --j) {}
                    // Just add space after closing any tag
                    if (!lastWasSpace) { out += ' '; lastWasSpace = true; }
                }
            }
            continue;
        }
        if (c == '<') {
            inTag = true;
            continue;
        }
        if (c == '&') {
            inEntity = true;
            entity.clear();
            continue;
        }
        if (inEntity) {
            if (c == ';') {
                inEntity = false;
                if (entity == "amp") out += '&';
                else if (entity == "lt") out += '<';
                else if (entity == "gt") out += '>';
                else if (entity == "quot") out += '"';
                else if (entity == "apos") out += '\'';
                else if (entity == "nbsp") { if (!lastWasSpace) { out += ' '; lastWasSpace = true; } }
                else if (entity == "#160" || entity == "#xa0") { if (!lastWasSpace) { out += ' '; lastWasSpace = true; } }
                else { out += '&'; out += entity; out += ';'; }
            } else {
                entity += c;
            }
            continue;
        }
        // Collapse whitespace
        bool isSpace = (c == ' ' || c == '\t' || c == '\r');
        if (c == '\n') {
            if (!lastWasSpace) { out += '\n'; lastWasSpace = true; }
            continue;
        }
        if (isSpace) {
            if (!lastWasSpace) { out += ' '; lastWasSpace = true; }
            continue;
        }
        out += c;
        lastWasSpace = false;
    }
    // Trim trailing whitespace
    while (!out.empty() && (out.back() == ' ' || out.back() == '\n')) out.pop_back();
    return out;
}

bool EmailTool::ResolveAccount(const std::string& accountName,
                                std::string& outInboxId,
                                std::string& outApiKey) {
    if (!m_configStore) return false;

    // Email accounts are stored as channel.{name}.config
    std::string configKey = "channel." + accountName + ".config";
    auto configStr = m_configStore->Get("", configKey);
    if (configStr.empty()) return false;

    auto config = ParseArgs(configStr);
    if (config.isNull()) return false;

    outInboxId = GetString(config, "inbox_id");
    outApiKey = GetString(config, "api_key");
    return !outInboxId.empty() && !outApiKey.empty();
}

bool EmailTool::ResolveAccountByInboxId(const std::string& inboxId,
                                        std::string& outInboxId,
                                        std::string& outApiKey) {
    // Search all channel configs for one whose inbox_id matches.
    // This handles the case where the agent passes an inbox_id (e.g.
    // "kestrelmolty@agentmail.to") instead of a channel name (e.g. "email").
    if (!m_configStore) return false;

    auto keys = m_configStore->ListKeys("");
    for (const auto& key : keys) {
        if (key.substr(0, 8) != "channel.") continue;
        if (key.find(".config") == std::string::npos) continue;

        auto configStr = m_configStore->Get("", key);
        auto config = ParseArgs(configStr);
        if (config.isNull()) continue;
        if (GetString(config, "backend") != "agentmail") continue;

        if (GetString(config, "inbox_id") == inboxId) {
            outInboxId = GetString(config, "inbox_id");
            outApiKey = GetString(config, "api_key");
            return !outApiKey.empty();
        }
    }
    return false;
}

bool EmailTool::FindFirstAgentMailAccount(std::string& outInboxId,
                                          std::string& outApiKey) {
    // Find the first AgentMail account in the config store.
    if (!m_configStore) return false;

    auto keys = m_configStore->ListKeys("");
    for (const auto& key : keys) {
        if (key.substr(0, 8) != "channel.") continue;
        if (key.find(".config") == std::string::npos) continue;

        auto configStr = m_configStore->Get("", key);
        auto config = ParseArgs(configStr);
        if (config.isNull()) continue;
        if (GetString(config, "backend") != "agentmail") continue;

        outInboxId = GetString(config, "inbox_id");
        outApiKey = GetString(config, "api_key");
        return !outInboxId.empty() && !outApiKey.empty();
    }
    return false;
}

// ============================================================================
// Action handlers
// ============================================================================

std::string EmailTool::HandleListInboxes(const Json::Value& /*args*/) {
    // List email channel accounts from config store
    Json::Value accounts(Json::arrayValue);

    if (m_configStore) {
        auto keys = m_configStore->ListKeys("");
            for (const auto& key : keys) {
            if (key.substr(0, 8) != "channel.") continue;
            // Pattern: channel.{name}.config where type is "email"
            if (key.substr(0, 8) != "channel.") continue;
            if (key.find(".config") == std::string::npos) continue;

            // Extract channel name to check the separate type key
            std::string chName = key.substr(8); // skip "channel."
            auto dotPos = chName.find(".config");
            if (dotPos != std::string::npos) chName = chName.substr(0, dotPos);
            std::string channelType = m_configStore->Get("", "channel." + chName + ".type");

            auto configStr = m_configStore->Get("", key);
            auto config = ParseArgs(configStr);
            if (config.isNull()) continue;
            // Check type from: (1) separate channel.{name}.type key,
            // (2) config JSON "backend" field, (3) config JSON "type" field
            if (channelType != "email" &&
                GetString(config, "backend") != "agentmail" &&
                GetString(config, "type") != "email") continue;

            Json::Value acct(Json::objectValue);
            acct["name"] = chName;
            acct["inbox_id"] = GetString(config, "inbox_id");
            acct["display_name"] = GetString(config, "display_name", chName);
            accounts.append(acct);
        }
    }

    Json::Value data(Json::objectValue);
    data["accounts"] = accounts;
    data["count"] = static_cast<int>(accounts.size());
    return OkResult(data);
}

std::string EmailTool::HandleListThreads(const Json::Value& args) {
    std::string inboxId = GetString(args, "inbox_id");
    std::string apiKey;

    // Resolve default account if no inbox_id specified
    if (inboxId.empty()) {
        // Try first email account
        if (!m_configStore) return ErrResult(400, "No email accounts configured");
        auto keys = m_configStore->ListKeys("");
        bool found = false;
        for (const auto& key : keys) {
            if (key.substr(0, 8) != "channel.") continue;
            if (key.find(".config") == std::string::npos) continue;
            auto configStr = m_configStore->Get("", key);
            auto config = ParseArgs(configStr);
            if (GetString(config, "backend") != "agentmail") continue;
            inboxId = GetString(config, "inbox_id");
            apiKey = GetString(config, "api_key");
            found = true;
            break;
        }
        if (!found) return ErrResult(400, "No email accounts configured");
    } else {
        // Resolve by inbox_id — find matching account
        if (!m_configStore) return ErrResult(400, "No config store");
        auto keys = m_configStore->ListKeys("");
        bool found = false;
        for (const auto& key : keys) {
            if (key.substr(0, 8) != "channel.") continue;
            if (key.find(".config") == std::string::npos) continue;
            auto configStr = m_configStore->Get("", key);
            auto config = ParseArgs(configStr);
            if (GetString(config, "inbox_id") == inboxId) {
                apiKey = GetString(config, "api_key");
                found = true;
                break;
            }
        }
        if (!found) return ErrResult(400, "Unknown inbox: " + inboxId);
    }

    std::map<std::string, std::string> query;
    int limit = GetInt(args, "limit", 20);
    query["limit"] = std::to_string(limit);
    std::string pageToken = GetString(args, "page_token");
    if (!pageToken.empty()) query["page_token"] = pageToken;
    std::string before = GetString(args, "before");
    if (!before.empty()) query["before"] = before;
    std::string after = GetString(args, "after");
    if (!after.empty()) query["after"] = after;

    auto resp = AgentMailGet(inboxId, "/v0/inboxes/" + UrlEncodePathSegment(inboxId) + "/threads", apiKey, query);
    if (resp.status_code != 200) {
        return ErrResult(resp.status_code,
            resp.error.empty() ? "AgentMail API error" : resp.error);
    }

    auto body = ParseArgs(resp.body);
    if (body.isNull()) return ErrResult(500, "Failed to parse AgentMail response");

    // Extract just what the agent needs — strip raw HTML bodies
    Json::Value data(Json::objectValue);
    data["threads"] = body["threads"];
    data["count"] = body["count"];
    if (body.isMember("next_page_token")) {
        data["next_page_token"] = body["next_page_token"];
    }
    return OkResult(data);
}

std::string EmailTool::HandleGetThread(const Json::Value& args) {
    std::string threadId = GetString(args, "thread_id");
    if (threadId.empty()) return ErrResult(400, "thread_id is required");

    std::string inboxId = GetString(args, "inbox_id");
    std::string apiKey;
    if (!ResolveAccount(inboxId, inboxId, apiKey)) {
        if (!ResolveAccountByInboxId(inboxId, inboxId, apiKey)) {
            if (!FindFirstAgentMailAccount(inboxId, apiKey)) {
                return ErrResult(400, "No email accounts configured");
            }
        }
    }

    auto resp = AgentMailGet(inboxId, "/v0/inboxes/" + UrlEncodePathSegment(inboxId) + "/threads/" + UrlEncodePathSegment(threadId), apiKey);
    if (resp.status_code != 200) {
        return ErrResult(resp.status_code,
            resp.error.empty() ? (ParseArgs(resp.body).isMember("message") ? ParseArgs(resp.body)["message"].asString() : std::string("Thread not found")) : resp.error);
    }

    auto body = ParseArgs(resp.body);
    if (body.isNull()) return ErrResult(500, "Failed to parse response");

    // Return thread with messages — prefer plain text
    return OkResult(body);
}

std::string EmailTool::HandleGetMessage(const Json::Value& args) {
    std::string messageId = GetString(args, "message_id");
    if (messageId.empty()) return ErrResult(400, "message_id is required");

    std::string inboxId = GetString(args, "inbox_id");
    std::string apiKey;
    if (!ResolveAccount(inboxId, inboxId, apiKey)) {
        if (!ResolveAccountByInboxId(inboxId, inboxId, apiKey)) {
            if (!FindFirstAgentMailAccount(inboxId, apiKey)) {
                return ErrResult(400, "No email accounts configured");
            }
        }
    }

    auto resp = AgentMailGet(inboxId,
        "/v0/inboxes/" + UrlEncodePathSegment(inboxId) + "/messages/" + UrlEncodePathSegment(messageId), apiKey);
    if (resp.status_code != 200) {
        return ErrResult(resp.status_code,
            resp.error.empty() ? (ParseArgs(resp.body).isMember("message") ? ParseArgs(resp.body)["message"].asString() : std::string("Message not found")) : resp.error);
    }

    auto body = ParseArgs(resp.body);
    if (body.isNull()) return ErrResult(500, "Failed to parse response");

    // Prefer plain text over HTML for agent consumption
    Json::Value data(Json::objectValue);
    // Copy all fields except raw html
    for (const auto& key : body.getMemberNames()) {
        if (key == "html") continue; // Skip raw HTML
        data[key] = body[key];
    }
    // If we have text, use it; otherwise note that HTML was stripped
    if (body.isMember("text") && body["text"].isString() && !body["text"].asString().empty()) {
        data["body"] = body["text"];
    } else if (body.isMember("html")) {
        data["body"] = StripHtml(body["html"].asString());
    }

    return OkResult(data);
}

std::string EmailTool::HandleGetAttachment(const Json::Value& args) {
    std::string messageId = GetString(args, "message_id");
    std::string attachmentId = GetString(args, "attachment_id");
    if (messageId.empty() || attachmentId.empty()) {
        return ErrResult(400, "message_id and attachment_id are required");
    }

    std::string inboxId = GetString(args, "inbox_id");
    std::string apiKey;
    if (!ResolveAccount(inboxId, inboxId, apiKey)) {
        if (!ResolveAccountByInboxId(inboxId, inboxId, apiKey)) {
            if (!FindFirstAgentMailAccount(inboxId, apiKey)) {
                return ErrResult(400, "No email accounts configured");
            }
        }
    }

    auto resp = AgentMailGet(inboxId,
        "/v0/inboxes/" + UrlEncodePathSegment(inboxId) + "/messages/" + UrlEncodePathSegment(messageId) +
        "/attachments/" + UrlEncodePathSegment(attachmentId), apiKey);
    if (resp.status_code != 200) {
        return ErrResult(resp.status_code,
            resp.error.empty() ? (ParseArgs(resp.body).isMember("message") ? ParseArgs(resp.body)["message"].asString() : std::string("Attachment not found")) : resp.error);
    }

    // #212: attachment-GET returns signed CDN URLs (~1h expiry, re-mintable)
    // — return them plus metadata; the content is durable server-side.
    auto att = ParseArgs(resp.body);
    if (att.isNull()) {
        return ErrResult(500, "Failed to parse attachment response");
    }
    Json::Value data(Json::objectValue);
    data["attachment_id"] = attachmentId;
    data["filename"] = att["filename"];
    data["content_type"] = att["content_type"];
    data["size"] = att["size"];
    data["download_url"] = att["download_url"];
    if (att.isMember("text_url") && att["text_url"].isString())
        data["text_url"] = att["text_url"];
    if (att.isMember("expires_at") && att["expires_at"].isString())
        data["expires_at"] = att["expires_at"];
    data["note"] = "Signed URLs expire (~1h); re-request to re-mint. "
                   "Attachment content is durable server-side.";
    return OkResult(data);
}

std::string EmailTool::HandleListAttachments(const Json::Value& args) {
    std::string messageId = GetString(args, "message_id");
    if (messageId.empty()) return ErrResult(400, "message_id is required");

    std::string inboxId = GetString(args, "inbox_id");
    std::string apiKey;
    if (!ResolveAccount(inboxId, inboxId, apiKey)) {
        if (!ResolveAccountByInboxId(inboxId, inboxId, apiKey)) {
            if (!FindFirstAgentMailAccount(inboxId, apiKey)) {
                return ErrResult(400, "No email accounts configured");
            }
        }
    }

    // #212: the message itself carries its attachment metadata array
    // (live-verified 2026-11-08: filename/size/content_type/
    // content_disposition/attachment_id per item).
    auto resp = AgentMailGet(inboxId,
        "/v0/inboxes/" + UrlEncodePathSegment(inboxId) + "/messages/" +
            UrlEncodePathSegment(messageId),
        apiKey);
    if (resp.status_code != 200) {
        auto errBody = ParseArgs(resp.body);
        return ErrResult(resp.status_code,
            errBody.isMember("message") ? errBody["message"].asString() : "Message not found");
    }

    auto body = ParseArgs(resp.body);
    if (body.isNull()) return ErrResult(500, "Failed to parse response");

    Json::Value data(Json::objectValue);
    Json::Value list(Json::arrayValue);
    if (body.isMember("attachments") && body["attachments"].isArray())
        list = body["attachments"];
    data["attachments"] = list;
    data["count"] = static_cast<Json::Int>(list.size());
    return OkResult(data);
}

std::string EmailTool::HandleSearch(const Json::Value& args) {
    // AgentMail doesn't have a dedicated search endpoint.
    // We use list_messages with before/after filtering.
    // For real search, we'd need to iterate — for now, return recent messages.
    std::string inboxId = GetString(args, "inbox_id");
    std::string apiKey;
    if (!ResolveAccount(inboxId, inboxId, apiKey)) {
        if (!ResolveAccountByInboxId(inboxId, inboxId, apiKey)) {
            if (!FindFirstAgentMailAccount(inboxId, apiKey)) {
                return ErrResult(400, "No email accounts configured");
            }
        }
    }

    std::map<std::string, std::string> query;
    query["limit"] = std::to_string(GetInt(args, "limit", 20));
    std::string before = GetString(args, "before");
    std::string after = GetString(args, "after");
    if (!before.empty()) query["before"] = before;
    if (!after.empty()) query["after"] = after;

    auto resp = AgentMailGet(inboxId,
        "/v0/inboxes/" + UrlEncodePathSegment(inboxId) + "/messages", apiKey, query);
    if (resp.status_code != 200) {
        return ErrResult(resp.status_code,
            resp.error.empty() ? (ParseArgs(resp.body).isMember("message") ? ParseArgs(resp.body)["message"].asString() : std::string("Search failed")) : resp.error);
    }

    auto body = ParseArgs(resp.body);
    if (body.isNull()) return ErrResult(500, "Failed to parse response");

    Json::Value data(Json::objectValue);
    data["messages"] = body["messages"];
    data["count"] = body["count"];
    if (body.isMember("next_page_token")) {
        data["next_page_token"] = body["next_page_token"];
    }
    return OkResult(data);
}

// --- #212: pure send-attachment payload builder (unit-testable) ---------

bool EmailTool::BuildSendAttachments(const Json::Value& args,
                                     Json::Value& outAttachments,
                                     std::string& outError) {
    outAttachments = Json::Value();
    outError.clear();
    if (!args.isMember("attachments")) return true;   // none: not an error
    const Json::Value& att = args["attachments"];
    if (att.isNull()) return true;                     // explicit null: none
    if (!att.isArray() || att.empty()) {
        outError = "attachments must be a non-empty array of "
                   "{filename, content} objects (base64 content)";
        return false;
    }
    Json::Value list(Json::arrayValue);
    for (const auto& item : att) {
        if (!item.isObject() || !item.isMember("filename") ||
            !item["filename"].isString() || item["filename"].asString().empty()) {
            outError = "each attachment needs a non-empty 'filename'";
            return false;
        }
        if (!item.isMember("content") || !item["content"].isString() ||
            item["content"].asString().empty()) {
            outError = "attachment '" + item["filename"].asString() +
                       "' needs non-empty base64 'content'";
            return false;
        }
        Json::Value a(Json::objectValue);
        a["filename"] = item["filename"];
        a["content"] = item["content"];
        list.append(a);
    }
    outAttachments = list;
    return true;
}

std::string EmailTool::HandleSend(const Json::Value& args) {
    std::string to = GetString(args, "to");
    if (to.empty()) return ErrResult(400, "to is required");

    std::string inboxId = GetString(args, "inbox_id");
    std::string apiKey;
    if (!ResolveAccount(inboxId, inboxId, apiKey)) {
        if (!ResolveAccountByInboxId(inboxId, inboxId, apiKey)) {
            if (!FindFirstAgentMailAccount(inboxId, apiKey)) {
                return ErrResult(400, "No email accounts configured");
            }
        }
    }

    Json::Value payload(Json::objectValue);
    // Handle to as string or array
    if (to.find(',') != std::string::npos) {
        Json::Value toArray(Json::arrayValue);
        std::istringstream ss(to);
        std::string addr;
        while (std::getline(ss, addr, ',')) {
            // trim
            auto start = addr.find_first_not_of(" \t");
            auto end = addr.find_last_not_of(" \t");
            if (start != std::string::npos)
                toArray.append(addr.substr(start, end - start + 1));
        }
        payload["to"] = toArray;
    } else {
        payload["to"] = to;
    }

    std::string subject = GetString(args, "subject");
    if (!subject.empty()) payload["subject"] = subject;
    std::string body = GetString(args, "body");
    if (!body.empty()) payload["text"] = body;

    // Optional CC/BCC
    std::string cc = GetString(args, "cc");
    if (!cc.empty()) payload["cc"] = cc;
    std::string bcc = GetString(args, "bcc");
    if (!bcc.empty()) payload["bcc"] = bcc;

    // #212: optional attachments [{filename, content: base64}]. Validated
    // client-side for shape; the server validates base64 as the backstop.
    // Receipt discipline: the send response can echo attachments: 0 on a
    // successful send — confirmation belongs at thread level, and a blind
    // retry double-delivers, so a non-200 here never silently retries.
    {
        Json::Value atts;
        std::string attErr;
        if (!BuildSendAttachments(args, atts, attErr)) {
            return ErrResult(400, attErr);
        }
        if (!atts.isNull()) payload["attachments"] = atts;
    }

    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    std::string bodyStr = Json::writeString(wb, payload);

    auto resp = AgentMailPost(inboxId,
        "/v0/inboxes/" + UrlEncodePathSegment(inboxId) + "/messages/send", bodyStr, apiKey);
    if (resp.status_code != 200) {
        auto errBody = ParseArgs(resp.body);
        return ErrResult(resp.status_code,
            errBody.isMember("message") ? errBody["message"].asString() : "Send failed");
    }

    auto result = ParseArgs(resp.body);
    return OkResult(result);
}

// --- #211: pure reply-recipient resolution (unit-testable) ----------------

bool EmailTool::ResolveReplyRecipient(const Json::Value& args,
                                      const Json::Value& anchor,
                                      bool anchorFetched,
                                      const std::string& selfInboxId,
                                      std::string& outRecipient,
                                      std::string& outError) {
    outRecipient.clear();
    outError.clear();

    // 1. Explicit to: honored in all cases (acceptance #2).
    const std::string explicitTo = GetString(args, "to");
    if (!explicitTo.empty()) {
        outRecipient = explicitTo;
        return true;
    }

    // 2. No explicit to: the anchor's envelope decides.
    if (!anchorFetched) {
        outError = "reply: cannot resolve recipient — anchor message not "
                   "fetchable and no explicit 'to' provided; refusing to "
                   "send rather than risk misdelivery";
        return false;
    }

    // "Name <addr>" -> bare address; bare passes through.
    auto bareAddr = [](const std::string& v) -> std::string {
        const auto lt = v.rfind('<');
        const auto gt = v.rfind('>');
        if (lt != std::string::npos && gt != std::string::npos && gt > lt)
            return v.substr(lt + 1, gt - lt - 1);
        return v;
    };
    auto labelContains = [&anchor](const char* needle) {
        if (!anchor.isMember("labels") || !anchor["labels"].isArray()) return false;
        for (const auto& l : anchor["labels"])
            if (l.isString() && l.asString() == needle) return true;
        return false;
    };

    if (labelContains("sent")) {
        // Own-sent anchor (#211's exact signature): the original
        // correspondent is the anchor's envelope To: — thread ownership
        // would otherwise resolve the reply to ourselves. A self-only
        // envelope (the self-addressed original this bug produced) is
        // unresolvable — error, never another silent self-delivery.
        if (!anchor.isMember("to") || !anchor["to"].isArray() ||
            anchor["to"].empty()) {
            outError = "reply: own-sent anchor carries no envelope recipient "
                       "('to' empty) and no explicit 'to' provided — cannot "
                       "resolve recipient";
            return false;
        }
        std::string joined;
        bool anyNonSelf = false;
        for (const auto& t : anchor["to"]) {
            if (!t.isString()) continue;
            const std::string addr = bareAddr(t.asString());
            if (addr.empty()) continue;
            if (!selfInboxId.empty() && addr == selfInboxId) continue;  // self drops out
            anyNonSelf = true;
            if (!joined.empty()) joined += ",";
            joined += addr;
        }
        if (joined.empty()) {
            outError = "reply: own-sent anchor's envelope recipient is this "
                       "inbox itself (self-addressed original) and no "
                       "explicit 'to' provided — refusing to send to self";
            return false;
        }
        (void)anyNonSelf;
        outRecipient = joined;
        return true;
    }

    // Received anchor: reply to its sender.
    if (!anchor.isMember("from") || !anchor["from"].isString() ||
        anchor["from"].asString().empty()) {
        outError = "reply: anchor carries no sender ('from' empty) and no "
                   "explicit 'to' provided — cannot resolve recipient";
        return false;
    }
    outRecipient = bareAddr(anchor["from"].asString());
    return true;
}

std::string EmailTool::HandleReply(const Json::Value& args) {
    std::string messageId = GetString(args, "message_id");
    if (messageId.empty()) return ErrResult(400, "message_id is required");
    std::string body = GetString(args, "body");
    if (body.empty()) return ErrResult(400, "body is required");

    std::string inboxId = GetString(args, "inbox_id");
    std::string apiKey;
    if (!ResolveAccount(inboxId, inboxId, apiKey)) {
        if (!ResolveAccountByInboxId(inboxId, inboxId, apiKey)) {
            if (!FindFirstAgentMailAccount(inboxId, apiKey)) {
                return ErrResult(400, "No email accounts configured");
            }
        }
    }

    // #211: resolve the recipient client-side. The server-side /reply
    // resolves from thread ownership, which self-addresses on own-sent
    // anchors — 200-confirmed, never delivered (six live incidents on
    // Buffett). We fetch the anchor's envelope and pass an explicit to:
    // so the server's ownership resolution never decides the recipient.
    // Explicit to: skips the fetch — the server validates the anchor
    // itself on the /reply call.
    Json::Value anchor;
    bool anchorFetched = false;
    if (GetString(args, "to").empty()) {
        auto resp = AgentMailGet(inboxId,
            "/v0/inboxes/" + UrlEncodePathSegment(inboxId) + "/messages/" +
                UrlEncodePathSegment(messageId),
            apiKey);
        if (resp.status_code == 200) {
            anchor = ParseArgs(resp.body);
            anchorFetched = !anchor.isNull();
        }
    }

    std::string recipient, resolveErr;
    if (!ResolveReplyRecipient(args, anchor, anchorFetched, inboxId,
                               recipient, resolveErr)) {
        return ErrResult(400, resolveErr);
    }

    Json::Value payload(Json::objectValue);
    payload["to"] = recipient;
    payload["text"] = body;

    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    std::string bodyStr = Json::writeString(wb, payload);

    auto resp = AgentMailPost(inboxId,
        "/v0/inboxes/" + UrlEncodePathSegment(inboxId) + "/messages/" + UrlEncodePathSegment(messageId) + "/reply",
        bodyStr, apiKey);
    if (resp.status_code != 200) {
        auto errBody = ParseArgs(resp.body);
        return ErrResult(resp.status_code,
            errBody.isMember("message") ? errBody["message"].asString() : "Reply failed");
    }

    auto result = ParseArgs(resp.body);

    // #211 verification discipline: 200 != delivered. Echo the resolved
    // recipient so the caller can cross-check (and so a self-addressed
    // resolution would be visible in the tool result, not silent).
    Json::Value verified = result.isNull() ? Json::Value(Json::objectValue) : result;
    verified["reply_to"] = recipient;
    return OkResult(verified);
}

std::string EmailTool::HandleForward(const Json::Value& args) {
    std::string messageId = GetString(args, "message_id");
    if (messageId.empty()) return ErrResult(400, "message_id is required");
    std::string to = GetString(args, "to");
    if (to.empty()) return ErrResult(400, "to is required");

    std::string inboxId = GetString(args, "inbox_id");
    std::string apiKey;
    if (!ResolveAccount(inboxId, inboxId, apiKey)) {
        if (!ResolveAccountByInboxId(inboxId, inboxId, apiKey)) {
            if (!FindFirstAgentMailAccount(inboxId, apiKey)) {
                return ErrResult(400, "No email accounts configured");
            }
        }
    }

    Json::Value payload(Json::objectValue);
    payload["to"] = to;
    std::string body = GetString(args, "body");
    if (!body.empty()) payload["text"] = body;

    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    std::string bodyStr = Json::writeString(wb, payload);

    auto resp = AgentMailPost(inboxId,
        "/v0/inboxes/" + UrlEncodePathSegment(inboxId) + "/messages/" + UrlEncodePathSegment(messageId) + "/forward",
        bodyStr, apiKey);
    if (resp.status_code != 200) {
        auto errBody = ParseArgs(resp.body);
        return ErrResult(resp.status_code,
            errBody.isMember("message") ? errBody["message"].asString() : "Forward failed");
    }

    auto result = ParseArgs(resp.body);
    return OkResult(result);
}

std::string EmailTool::HandleMarkRead(const Json::Value& args) {
    std::string messageId = GetString(args, "message_id");
    if (messageId.empty()) return ErrResult(400, "message_id is required");

    std::string inboxId = GetString(args, "inbox_id");
    std::string apiKey;
    if (!ResolveAccount(inboxId, inboxId, apiKey)) {
        if (!ResolveAccountByInboxId(inboxId, inboxId, apiKey)) {
            if (!FindFirstAgentMailAccount(inboxId, apiKey)) {
                return ErrResult(400, "No email accounts configured");
            }
        }
    }

    // AgentMail uses update message with labels
    Json::Value payload(Json::objectValue);
    Json::Value labels(Json::arrayValue);
    labels.append("read");
    payload["labels"] = labels;

    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    std::string bodyStr = Json::writeString(wb, payload);

    auto resp = AgentMailPut(inboxId,
        "/v0/inboxes/" + UrlEncodePathSegment(inboxId) + "/messages/" + UrlEncodePathSegment(messageId), bodyStr, apiKey);
    if (resp.status_code != 200) {
        return ErrResult(resp.status_code, "Failed to mark as read");
    }

    Json::Value data(Json::objectValue);
    data["message_id"] = messageId;
    data["marked"] = "read";
    return OkResult(data);
}

std::string EmailTool::HandleDelete(const Json::Value& args) {
    std::string messageId = GetString(args, "message_id");
    if (messageId.empty()) return ErrResult(400, "message_id is required");

    std::string inboxId = GetString(args, "inbox_id");
    std::string apiKey;
    if (!ResolveAccount(inboxId, inboxId, apiKey)) {
        if (!ResolveAccountByInboxId(inboxId, inboxId, apiKey)) {
            if (!FindFirstAgentMailAccount(inboxId, apiKey)) {
                return ErrResult(400, "No email accounts configured");
            }
        }
    }

    auto resp = AgentMailDelete(inboxId,
        "/v0/inboxes/" + UrlEncodePathSegment(inboxId) + "/messages/" + UrlEncodePathSegment(messageId), apiKey);
    if (resp.status_code != 200 && resp.status_code != 204) {
        return ErrResult(resp.status_code, "Delete failed");
    }

    Json::Value data(Json::objectValue);
    data["message_id"] = messageId;
    data["deleted"] = true;
    return OkResult(data);
}

std::string EmailTool::HandleListDrafts(const Json::Value& args) {
    std::string inboxId = GetString(args, "inbox_id");
    std::string apiKey;
    if (!ResolveAccount(inboxId, inboxId, apiKey)) {
        if (!ResolveAccountByInboxId(inboxId, inboxId, apiKey)) {
            if (!FindFirstAgentMailAccount(inboxId, apiKey)) {
                return ErrResult(400, "No email accounts configured");
            }
        }
    }

    std::map<std::string, std::string> query;
    query["limit"] = std::to_string(GetInt(args, "limit", 20));
    std::string pageToken = GetString(args, "page_token");
    if (!pageToken.empty()) query["page_token"] = pageToken;

    auto resp = AgentMailGet(inboxId,
        "/v0/inboxes/" + UrlEncodePathSegment(inboxId) + "/drafts", apiKey, query);
    if (resp.status_code != 200) {
        return ErrResult(resp.status_code, "Failed to list drafts");
    }

    auto body = ParseArgs(resp.body);
    return OkResult(body);
}

std::string EmailTool::HandleCreateDraft(const Json::Value& args) {
    std::string inboxId = GetString(args, "inbox_id");
    std::string apiKey;
    if (!ResolveAccount(inboxId, inboxId, apiKey)) {
        if (!ResolveAccountByInboxId(inboxId, inboxId, apiKey)) {
            if (!FindFirstAgentMailAccount(inboxId, apiKey)) {
                return ErrResult(400, "No email accounts configured");
            }
        }
    }

    Json::Value payload(Json::objectValue);
    std::string to = GetString(args, "to");
    if (!to.empty()) payload["to"] = to;
    std::string subject = GetString(args, "subject");
    if (!subject.empty()) payload["subject"] = subject;
    std::string body = GetString(args, "body");
    if (!body.empty()) payload["text"] = body;

    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    std::string bodyStr = Json::writeString(wb, payload);

    auto resp = AgentMailPost(inboxId,
        "/v0/inboxes/" + UrlEncodePathSegment(inboxId) + "/drafts", bodyStr, apiKey);
    if (resp.status_code != 200) {
        return ErrResult(resp.status_code, "Failed to create draft");
    }

    auto result = ParseArgs(resp.body);
    return OkResult(result);
}

std::string EmailTool::HandleSendDraft(const Json::Value& args) {
    std::string draftId = GetString(args, "draft_id");
    if (draftId.empty()) return ErrResult(400, "draft_id is required");

    std::string inboxId = GetString(args, "inbox_id");
    std::string apiKey;
    if (!ResolveAccount(inboxId, inboxId, apiKey)) {
        if (!ResolveAccountByInboxId(inboxId, inboxId, apiKey)) {
            if (!FindFirstAgentMailAccount(inboxId, apiKey)) {
                return ErrResult(400, "No email accounts configured");
            }
        }
    }

    auto resp = AgentMailPost(inboxId,
        "/v0/inboxes/" + UrlEncodePathSegment(inboxId) + "/drafts/" + UrlEncodePathSegment(draftId) + "/send",
        "{}", apiKey);
    if (resp.status_code != 200) {
        return ErrResult(resp.status_code, "Failed to send draft");
    }

    auto result = ParseArgs(resp.body);
    return OkResult(result);
}

} // namespace animus::kernel

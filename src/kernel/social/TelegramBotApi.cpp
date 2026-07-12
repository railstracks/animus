#include "animus_kernel/social/TelegramBotApi.h"
#include "animus_kernel/tools/HttpClient.h"

#include <json/json.h>
#include <sstream>
#include <iostream>

namespace animus::kernel::telegram {

// ============================================================================
// JSON helpers (local)
// ============================================================================

namespace {

Json::Value ParseJson(const std::string& json) {
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::istringstream stream(json);
    std::string errors;
    Json::parseFromStream(builder, stream, &root, &errors);
    return root;
}

} // anonymous namespace

// ============================================================================
// TelegramBotApi implementation
// ============================================================================

TelegramBotApi::TelegramBotApi(HttpClient& httpClient)
    : m_httpClient(httpClient) {
}

std::string TelegramBotApi::MakeUrl(const std::string& token,
                                     const std::string& method) {
    return "https://api.telegram.org/bot" + token + "/" + method;
}

// ---------------------------------------------------------------------------
// Low-level API call
// ---------------------------------------------------------------------------

std::optional<Json::Value> TelegramBotApi::Call(const std::string& token,
                                                  const std::string& method,
                                                  const std::string& body,
                                                  const std::string& contentType) {
    HttpClient::Request req;
    req.url = MakeUrl(token, method);
    req.method = "POST";
    req.headers["Content-Type"] = contentType;
    req.body = body;
    req.timeout_seconds = 60; // Long poll may take up to 30s + network

    auto resp = m_httpClient.Execute(req);
    if (resp.status_code != 200) {
        std::cerr << "[telegram:api] " << method << " HTTP " << resp.status_code
                  << " body=" << resp.body.substr(0, 300) << std::endl;
        return std::nullopt;
    }

    auto root = ParseJson(resp.body);
    if (!root.isMember("ok") || !root["ok"].asBool()) {
        std::string desc = root.isMember("description")
            ? root["description"].asString() : "unknown error";
        int code = root.isMember("error_code") ? root["error_code"].asInt() : 0;
        std::cerr << "[telegram:api] " << method << " error " << code
                  << ": " << desc << std::endl;
        return std::nullopt;
    }

    if (root.isMember("result")) {
        return root["result"];
    }
    return Json::Value{};
}

// ---------------------------------------------------------------------------
// getMe
// ---------------------------------------------------------------------------

std::optional<BotInfo> TelegramBotApi::GetMe(const std::string& token) {
    auto result = Call(token, "getMe", "");
    if (!result || !result->isObject()) return std::nullopt;
    return BotInfo::FromJson(*result);
}

// ---------------------------------------------------------------------------
// getUpdates
// ---------------------------------------------------------------------------

std::vector<Update> TelegramBotApi::GetUpdates(const std::string& token,
                                                 GetUpdatesOptions opts) {
    Json::Value params(Json::objectValue);
    if (opts.offset > 0) params["offset"] = Json::Int64(opts.offset);
    if (opts.limit > 0 && opts.limit != 100) params["limit"] = opts.limit;
    if (opts.timeout > 0) params["timeout"] = opts.timeout;

    if (!opts.allowed_updates.empty()) {
        Json::Value arr(Json::arrayValue);
        for (const auto& u : opts.allowed_updates)
            arr.append(u);
        params["allowed_updates"] = arr;
    }

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    std::string body = Json::writeString(wb, params);

    // Long poll: use extended timeout
    int prevTimeout = 60;
    // TODO: HttpClient should support per-request timeout override.
    // For now, the default 60s should suffice for a 30s long poll.

    auto result = Call(token, "getUpdates", body);
    if (!result || !result->isArray()) return {};

    std::vector<Update> updates;
    for (const auto& item : *result) {
        updates.push_back(Update::FromJson(item));
    }
    return updates;
}

// ---------------------------------------------------------------------------
// sendMessage
// ---------------------------------------------------------------------------

std::optional<int64_t> TelegramBotApi::SendMessage(
        const std::string& token,
        const SendMessageOptions& opts) {

    Json::Value params(Json::objectValue);
    params["chat_id"] = Json::Int64(opts.chat_id);
    params["text"] = opts.text;

    if (opts.reply_to_message_id > 0)
        params["reply_to_message_id"] = Json::Int64(opts.reply_to_message_id);
    if (opts.message_thread_id > 0)
        params["message_thread_id"] = Json::Int64(opts.message_thread_id);
    if (!opts.parse_mode.empty())
        params["parse_mode"] = opts.parse_mode;
    if (opts.disable_notification)
        params["disable_notification"] = true;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    std::string body = Json::writeString(wb, params);

    auto result = Call(token, "sendMessage", body);
    if (!result || !result->isObject()) return std::nullopt;
    if (result->isMember("message_id"))
        return (*result)["message_id"].asInt64();
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// sendChatAction
// ---------------------------------------------------------------------------

bool TelegramBotApi::SendChatAction(const std::string& token,
                                      int64_t chat_id,
                                      const std::string& action) {
    Json::Value params(Json::objectValue);
    params["chat_id"] = Json::Int64(chat_id);
    params["action"] = action;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    std::string body = Json::writeString(wb, params);

    auto result = Call(token, "sendChatAction", body);
    return result.has_value();
}

// ---------------------------------------------------------------------------
// editMessageText
// ---------------------------------------------------------------------------

std::optional<int64_t> TelegramBotApi::EditMessageText(
        const std::string& token,
        int64_t chat_id,
        int64_t message_id,
        const std::string& text,
        const std::string& parse_mode) {

    Json::Value params(Json::objectValue);
    params["chat_id"] = Json::Int64(chat_id);
    params["message_id"] = Json::Int64(message_id);
    params["text"] = text;
    if (!parse_mode.empty())
        params["parse_mode"] = parse_mode;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    std::string body = Json::writeString(wb, params);

    auto result = Call(token, "editMessageText", body);
    if (!result || !result->isObject()) return std::nullopt;
    if (result->isMember("message_id"))
        return (*result)["message_id"].asInt64();
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// deleteMessage
// ---------------------------------------------------------------------------

bool TelegramBotApi::DeleteMessage(const std::string& token,
                                     int64_t chat_id,
                                     int64_t message_id) {
    Json::Value params(Json::objectValue);
    params["chat_id"] = Json::Int64(chat_id);
    params["message_id"] = Json::Int64(message_id);

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    std::string body = Json::writeString(wb, params);

    auto result = Call(token, "deleteMessage", body);
    return result.has_value();
}

// ---------------------------------------------------------------------------
// setMessageReaction
// ---------------------------------------------------------------------------

bool TelegramBotApi::SetMessageReaction(const std::string& token,
                                          int64_t chat_id,
                                          int64_t message_id,
                                          const std::string& emoji) {
    Json::Value params(Json::objectValue);
    params["chat_id"] = Json::Int64(chat_id);
    params["message_id"] = Json::Int64(message_id);

    Json::Value reaction(Json::arrayValue);
    Json::Value reactionObj(Json::objectValue);
    reactionObj["type"] = "emoji";
    reactionObj["emoji"] = emoji;
    reaction.append(reactionObj);
    params["reaction"] = reaction;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    std::string body = Json::writeString(wb, params);

    auto result = Call(token, "setMessageReaction", body);
    return result.has_value();
}

// ---------------------------------------------------------------------------
// getChat
// ---------------------------------------------------------------------------

std::optional<Chat> TelegramBotApi::GetChat(const std::string& token,
                                              int64_t chat_id) {
    Json::Value params(Json::objectValue);
    params["chat_id"] = Json::Int64(chat_id);

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    std::string body = Json::writeString(wb, params);

    auto result = Call(token, "getChat", body);
    if (!result || !result->isObject()) return std::nullopt;
    return Chat::FromJson(*result);
}

} // namespace animus::kernel::telegram

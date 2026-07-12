#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <json/json.h>
#include "animus_kernel/social/TelegramTypes.h"

namespace animus::kernel {

class HttpClient;

namespace telegram {

// ============================================================================
// TelegramBotApi — HTTP client for the Telegram Bot API
//
// Wraps the Bot API HTTPS interface. All methods take a bot token and
// return parsed JSON or typed structs. Thread-safe (stateless HTTP calls).
// ============================================================================

class TelegramBotApi {
public:
    explicit TelegramBotApi(HttpClient& httpClient);

    // --- Bot info ---
    std::optional<BotInfo> GetMe(const std::string& token);

    // --- Updates ---
    struct GetUpdatesOptions {
        int64_t offset{0};
        int limit{100};             // 1-100
        int timeout{30};            // Long poll timeout in seconds
        std::vector<std::string> allowed_updates;
    };

    std::vector<Update> GetUpdates(const std::string& token,
                                    GetUpdatesOptions opts);

    // --- Sending messages ---
    struct SendMessageOptions {
        int64_t chat_id{0};
        std::string text;
        int64_t reply_to_message_id{0};
        int64_t message_thread_id{0};
        std::string parse_mode;     // "MarkdownV2", "HTML", or empty
        bool disable_notification{false};
    };

    std::optional<int64_t> SendMessage(const std::string& token,
                                        const SendMessageOptions& opts);

    // --- Chat actions (typing indicator etc.) ---
    bool SendChatAction(const std::string& token,
                        int64_t chat_id,
                        const std::string& action); // "typing", "upload_photo", etc.

    // --- Message editing ---
    std::optional<int64_t> EditMessageText(const std::string& token,
                                            int64_t chat_id,
                                            int64_t message_id,
                                            const std::string& text,
                                            const std::string& parse_mode = "");

    // --- Message deletion ---
    bool DeleteMessage(const std::string& token,
                       int64_t chat_id,
                       int64_t message_id);

    // --- Reactions ---
    bool SetMessageReaction(const std::string& token,
                            int64_t chat_id,
                            int64_t message_id,
                            const std::string& emoji); // e.g. "👍"

    // --- Chat info ---
    std::optional<Chat> GetChat(const std::string& token,
                                int64_t chat_id);

    // --- Low-level ---
    // Execute an arbitrary Bot API method, return the "result" field.
    // Returns nullopt if the request fails or ok != true.
    std::optional<Json::Value> Call(const std::string& token,
                                    const std::string& method,
                                    const std::string& body,
                                    const std::string& contentType = "application/json");

private:
    static std::string MakeUrl(const std::string& token,
                               const std::string& method);

    HttpClient& m_httpClient;
};

} // namespace telegram
} // namespace animus::kernel

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <json/json.h>

namespace animus::kernel::telegram {

// ============================================================================
// Telegram Bot API type definitions
//
// Lightweight structs for the Telegram objects relevant to Animus.
// Parsing from JSON is done via static FromJson() factory methods.
// ============================================================================

// ---------------------------------------------------------------------------
// Core types
// ---------------------------------------------------------------------------

struct User {
    int64_t id{0};
    bool is_bot{false};
    std::string first_name;
    std::string last_name;
    std::string username;
    std::string language_code;

    std::string DisplayName() const {
        if (!first_name.empty() && !last_name.empty())
            return first_name + " " + last_name;
        return first_name;
    }

    static User FromJson(const Json::Value& v) {
        User u;
        u.id = v.isMember("id") ? v["id"].asInt64() : 0;
        u.is_bot = v.isMember("is_bot") && v["is_bot"].asBool();
        u.first_name = v.isMember("first_name") ? v["first_name"].asString() : "";
        u.last_name = v.isMember("last_name") ? v["last_name"].asString() : "";
        u.username = v.isMember("username") ? v["username"].asString() : "";
        u.language_code = v.isMember("language_code") ? v["language_code"].asString() : "";
        return u;
    }
};

struct Chat {
    int64_t id{0};
    std::string type;          // "private", "group", "supergroup", "channel"
    std::string title;
    std::string username;
    std::string first_name;
    std::string last_name;

    std::string DisplayName() const {
        if (type == "private") {
            if (!first_name.empty() && !last_name.empty())
                return first_name + " " + last_name;
            return first_name;
        }
        return title;
    }

    bool IsPrivate() const { return type == "private"; }
    bool IsGroup() const { return type == "group" || type == "supergroup"; }
    bool IsForum() const { return type == "supergroup"; } // forums are supergroups with is_forum=true

    static Chat FromJson(const Json::Value& v) {
        Chat c;
        c.id = v.isMember("id") ? v["id"].asInt64() : 0;
        c.type = v.isMember("type") ? v["type"].asString() : "";
        c.title = v.isMember("title") ? v["title"].asString() : "";
        c.username = v.isMember("username") ? v["username"].asString() : "";
        c.first_name = v.isMember("first_name") ? v["first_name"].asString() : "";
        c.last_name = v.isMember("last_name") ? v["last_name"].asString() : "";
        return c;
    }
};

// ---------------------------------------------------------------------------
// Message
// ---------------------------------------------------------------------------

struct Message {
    int64_t message_id{0};
    int64_t date{0};           // Unix timestamp
    Chat chat;
    User from;                 // Sender (may be empty for channel posts)
    std::string text;
    int64_t message_thread_id{0};  // Forum topic / thread ID (0 = not set)

    // Reply context
    bool has_reply{false};

    // Forward origin (simplified)
    std::string forward_origin; // "user:<name>" or empty

    static Message FromJson(const Json::Value& v) {
        Message m;
        m.message_id = v.isMember("message_id") ? v["message_id"].asInt64() : 0;
        m.date = v.isMember("date") ? v["date"].asInt64() : 0;
        m.text = v.isMember("text") ? v["text"].asString() : "";
        m.message_thread_id = v.isMember("message_thread_id")
            ? v["message_thread_id"].asInt64() : 0;
        m.has_reply = v.isMember("reply_to_message");

        if (v.isMember("chat")) m.chat = Chat::FromJson(v["chat"]);
        if (v.isMember("from")) m.from = User::FromJson(v["from"]);

        // Simplified forward origin
        if (v.isMember("forward_origin")) {
            auto& fo = v["forward_origin"];
            std::string originType = fo.isMember("type") ? fo["type"].asString() : "";
            if (originType == "user" && fo.isMember("sender_user")) {
                m.forward_origin = "user:" + User::FromJson(fo["sender_user"]).DisplayName();
            }
        }

        return m;
    }
};

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

struct Update {
    int64_t update_id{0};

    enum class Type {
        Unknown,
        Message,
        EditedMessage,
        ChannelPost,
        EditedChannelPost,
        CallbackQuery,
        MyChatMember,
    };

    Type type{Type::Unknown};
    Message message;

    // Callback query data (for inline keyboard buttons)
    std::string callback_data;
    int64_t callback_message_id{0};
    std::string callback_chat_id;

    // MyChatMember — bot added/removed from chat
    std::string member_status;  // "member", "left", "kicked", etc.
    int64_t member_chat_id{0};

    static Update FromJson(const Json::Value& v) {
        Update u;
        u.update_id = v.isMember("update_id") ? v["update_id"].asInt64() : 0;

        if (v.isMember("message")) {
            u.type = Type::Message;
            u.message = Message::FromJson(v["message"]);
        } else if (v.isMember("edited_message")) {
            u.type = Type::EditedMessage;
            u.message = Message::FromJson(v["edited_message"]);
        } else if (v.isMember("channel_post")) {
            u.type = Type::ChannelPost;
            u.message = Message::FromJson(v["channel_post"]);
        } else if (v.isMember("edited_channel_post")) {
            u.type = Type::EditedChannelPost;
            u.message = Message::FromJson(v["edited_channel_post"]);
        } else if (v.isMember("callback_query")) {
            u.type = Type::CallbackQuery;
            auto& cq = v["callback_query"];
            if (cq.isMember("data")) u.callback_data = cq["data"].asString();
            if (cq.isMember("message") && cq["message"].isMember("chat")) {
                u.callback_chat_id = std::to_string(
                    cq["message"]["chat"]["id"].asInt64());
                if (cq["message"].isMember("message_id"))
                    u.callback_message_id = cq["message"]["message_id"].asInt64();
            }
        } else if (v.isMember("my_chat_member")) {
            u.type = Type::MyChatMember;
            auto& cm = v["my_chat_member"];
            if (cm.isMember("new_chat_member") && cm["new_chat_member"].isMember("status"))
                u.member_status = cm["new_chat_member"]["status"].asString();
            if (cm.isMember("chat") && cm["chat"].isMember("id"))
                u.member_chat_id = cm["chat"]["id"].asInt64();
        }

        return u;
    }
};

// ---------------------------------------------------------------------------
// Bot info (getMe response)
// ---------------------------------------------------------------------------

struct BotInfo {
    int64_t id{0};
    std::string username;
    std::string first_name;

    bool valid{false};

    static BotInfo FromJson(const Json::Value& result) {
        BotInfo b;
        b.id = result.isMember("id") ? result["id"].asInt64() : 0;
        b.username = result.isMember("username") ? result["username"].asString() : "";
        b.first_name = result.isMember("first_name") ? result["first_name"].asString() : "";
        b.valid = (b.id != 0 && !b.username.empty());
        return b;
    }
};

} // namespace animus::kernel::telegram

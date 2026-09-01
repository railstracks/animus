#include "animus_kernel/Log.h"
#include "animus_kernel/ChannelAdapters.h"
#include "animus_kernel/ChannelContext.h"
#include "animus_kernel/ChannelHelpers.h"

#include <iostream>

#include "animus_kernel/ChannelState.h"
#include "animus_kernel/AgentConfigStore.h"
#include "animus_kernel/ChannelManager.h"
#include "animus_kernel/tools/HttpClient.h"
#include "animus_kernel/social/TelegramTypes.h"
#include "animus_kernel/social/TelegramBotApi.h"

#include <json/json.h>

namespace animus::kernel {

using namespace channel_detail;

// ============================================================================
// TelegramAdapter
// ============================================================================

void TelegramAdapter::RunLoop() {
    auto* rt = m_runtime.get();
    ALOG_DEBUG("telegram", "Long Poll loop starting for " << rt->channel_name);

    std::string token = GetString(rt->config, "access_token");
    if (token.empty()) {
        ALOG_WARNING("telegram", "No access token for " << rt->channel_name);
        rt->active = false;
        return;
    }

    telegram::TelegramBotApi api(m_ctx.httpClient);

    auto botInfo = api.GetMe(token);
    if (!botInfo) {
        ALOG_WARNING("telegram", "getMe failed for " << rt->channel_name
                  << " — check bot token");
        rt->active = false;
        return;
    }
    ALOG_INFO("telegram", "Connected as @" << botInfo->username
              << " (" << botInfo->first_name << ", id=" << botInfo->id << ")");

    while (rt->active && !m_stopRequested) {
        try {
            auto now = std::chrono::steady_clock::now();
            if (now < rt->next_attempt) {
                auto sleepMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    rt->next_attempt - now).count();
                std::this_thread::sleep_for(std::chrono::milliseconds(
                    std::min(sleepMs, (int64_t)5000)));
                continue;
            }

            telegram::TelegramBotApi::GetUpdatesOptions opts;
            opts.offset = (rt->last_update_id > 0) ? rt->last_update_id + 1 : 0;
            opts.timeout = GetInt(rt->config, "polling.wait", 25);

            auto updates = api.GetUpdates(token, opts);
            if (m_stopRequested) break;

            for (auto& update : updates) {
                rt->last_update_id = std::max(rt->last_update_id, update.update_id);

                switch (update.type) {
                    case telegram::Update::Type::Message:
                    case telegram::Update::Type::EditedMessage:
                    case telegram::Update::Type::ChannelPost:
                    case telegram::Update::Type::EditedChannelPost:
                        ProcessMessage(update.message);
                        break;
                    case telegram::Update::Type::MyChatMember:
                        ProcessChatMemberUpdate(update);
                        break;
                    default:
                        break;
                }
            }

            if (!updates.empty() && m_ctx.configStore) {
                m_ctx.configStore->Set("", "channel." + rt->channel_name + ".polling.last_update_id",
                                       std::to_string(rt->last_update_id));
            }

            rt->consecutive_errors = 0;
        } catch (const std::exception& ex) {
            ALOG_ERROR("telegram", "Exception for " << rt->channel_name
                      << ": " << ex.what());
            rt->consecutive_errors++;
            int backoff = std::min(60, rt->consecutive_errors * 5);
            rt->next_attempt = std::chrono::steady_clock::now() + std::chrono::seconds(backoff);
        }
    }

    ALOG_DEBUG("telegram", "Loop ended for " << rt->channel_name);
}

void TelegramAdapter::ProcessMessage(const telegram::Message& msg) {
    if (msg.text.empty()) return;

    auto* rt = m_runtime.get();
    std::string routingKey = "peer:" + std::to_string(msg.chat.id);
    if (msg.message_thread_id > 0) {
        routingKey += ":" + std::to_string(msg.message_thread_id);
    }

    // Body attribution (#15/#42 pattern): per-message origin header, no
    // inline delivery hints — the context card carries reply semantics.
    // DMs minimal; groups carry sender + chat. Channel posts may be
    // anonymous (empty `from`) — the chat name carries attribution then.
    std::string senderName = msg.from.DisplayName();
    std::string chatName = msg.chat.DisplayName();
    bool isPrivate = msg.chat.IsPrivate();

    std::string displayText;
    if (isPrivate) {
        displayText = "private message from " + senderName
            + " (user:" + std::to_string(msg.from.id) + "):\n" + msg.text;
    } else {
        std::string who = senderName.empty()
            ? chatName + " (channel post)"
            : senderName + " (user:" + std::to_string(msg.from.id) + ")";
        displayText = "Telegram message from " + who + " in " + chatName;
        if (msg.message_thread_id > 0) displayText += " (topic thread)";
        displayText += ":\n" + msg.text;
    }

    // Context card (#15/#42) — tool-only delivery: chats may be groups,
    // so silence must be a first-class outcome.
    Json::Value meta;
    meta["message_type"] = "chat";
    meta["delivery"] = "tool";
    Json::Value origin;
    if (!senderName.empty()) origin["user_display"] = senderName;
    if (msg.from.id != 0) origin["user_id"] = std::to_string(msg.from.id);
    if (!isPrivate) {
        origin["channel"] = chatName;
        origin["channel_id"] = std::to_string(msg.chat.id);
    }
    meta["origin"] = origin;
    meta["chat_id"] = std::to_string(msg.chat.id);
    meta["chat_type"] = isPrivate ? "dm" : "group";
    if (msg.message_thread_id > 0)
        meta["topic_thread_id"] = std::to_string(msg.message_thread_id);
    if (msg.message_id > 0)
        meta["source_message_id"] = std::to_string(msg.message_id);
    meta["reply_instructions"] =
        "Reply using the channels tool with action=reply; chat_id is "
        "provided by the arrival and filled automatically. Text replies "
        "are NOT delivered. In group chats, if the message is not "
        "addressed to you, staying silent is correct.";
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    std::string metadata = Json::writeString(wb, meta);

    std::string sessionType = isPrivate ? "telegram:private" : "telegram:group";
    Dispatch(routingKey, displayText, sessionType, metadata);
}

void TelegramAdapter::ProcessChatMemberUpdate(const telegram::Update& update) {
    std::string status = update.member_status;
    std::string chatId = std::to_string(update.member_chat_id);

    if (status == "member" || status == "administrator") {
        ALOG_DEBUG("telegram", "Bot added to chat " << chatId
                  << " (status: " << status << ")");
    } else if (status == "left" || status == "kicked") {
        ALOG_DEBUG("telegram", "Bot removed from chat " << chatId
                  << " (status: " << status << ")");
    }
}

namespace {

// Telegram message size limit
constexpr size_t kTelegramMaxMsgLen = 4096;

// Split text into chunks ≤ maxLen, trying to break on natural boundaries.
std::vector<std::string> SplitForTelegram(const std::string& text, size_t maxLen) {
    if (text.size() <= maxLen) return {text};

    std::vector<std::string> chunks;
    size_t pos = 0;

    while (pos < text.size()) {
        size_t end = pos + maxLen;
        if (end >= text.size()) {
            chunks.push_back(text.substr(pos));
            break;
        }

        // Try to find a good break point: prefer double newline, then single newline,
        // then space, then last resort: hard cut.
        size_t breakPoint = std::string::npos;

        // Search backwards from end for "\n\n"
        size_t searchFrom = (end > 100) ? end - 100 : pos;
        size_t dd = text.rfind("\n\n", end);
        if (dd != std::string::npos && dd > searchFrom) {
            breakPoint = dd + 2;
        } else {
            // Single "\n"
            size_t sd = text.rfind('\n', end);
            if (sd != std::string::npos && sd > searchFrom) {
                breakPoint = sd + 1;
            } else {
                // Space
                size_t sp = text.rfind(' ', end);
                if (sp != std::string::npos && sp > searchFrom) {
                    breakPoint = sp + 1;
                } else {
                    // Hard cut
                    breakPoint = end;
                }
            }
        }

        chunks.push_back(text.substr(pos, breakPoint - pos));
        pos = breakPoint;
    }

    return chunks;
}

} // namespace

void TelegramAdapter::SendReply(const ChannelReplyTarget& target, const std::string& text) {
    auto* rt = m_runtime.get();
    std::string token = GetString(rt->config, "access_token");
    if (token.empty()) {
        ALOG_WARNING("telegram", "Reply: no access token for " << target.channel_name);
        return;
    }

    telegram::TelegramBotApi api(m_ctx.httpClient);
    // peer_id is "chat_id" or "chat_id:thread_id" (forum topics route the
    // latter; the thread id must ride sendMessage or the reply lands in
    // General / fails in topics-only supergroups).
    int64_t chatId = 0;
    int64_t threadId = 0;
    auto colon = target.peer_id.find(':');
    try {
        if (colon != std::string::npos) {
            chatId = std::stoll(target.peer_id.substr(0, colon));
            threadId = std::stoll(target.peer_id.substr(colon + 1));
        } else {
            chatId = std::stoll(target.peer_id);
        }
    } catch (...) {
        ALOG_WARNING("telegram", "Reply: invalid chat_id: " << target.peer_id);
        return;
    }

    auto chunks = SplitForTelegram(text, kTelegramMaxMsgLen);
    for (size_t i = 0; i < chunks.size(); ++i) {
        telegram::TelegramBotApi::SendMessageOptions opts;
        opts.chat_id = chatId;
        opts.message_thread_id = threadId;
        opts.text = chunks[i];

        auto msgId = api.SendMessage(token, opts);
        if (msgId) {
            ALOG_DEBUG("telegram", "Message sent to " << target.peer_id
                      << " (msg_id=" << *msgId << ")"
                      << (chunks.size() > 1 ? " [" + std::to_string(i + 1) + "/" + std::to_string(chunks.size()) + "]" : ""));
        } else {
            ALOG_WARNING("telegram", "Send failed to " << target.peer_id
                        << " [chunk " << (i + 1) << "/" << chunks.size() << "]");
            // Continue sending remaining chunks even if one fails
        }
    }
}

} // namespace animus::kernel

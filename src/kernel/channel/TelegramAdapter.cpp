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

    std::string senderName = msg.from.DisplayName();
    std::string chatName = msg.chat.DisplayName();
    std::string prompt;
    const std::string replyHint = "\n\nYou are responding via Telegram. Respond naturally — "
        "your reply will be sent automatically. Do NOT use the social tool to reply.";

    if (msg.chat.IsPrivate()) {
        prompt = "New Telegram message from " + senderName + ":\n" + msg.text + replyHint;
    } else {
        prompt = "Telegram message from " + senderName + " in " + chatName;
        if (msg.message_thread_id > 0) prompt += " (topic thread)";
        prompt += ":\n" + msg.text + replyHint;
    }

    std::string sessionType = msg.chat.IsPrivate() ? "telegram:private" : "telegram:group";
    Dispatch(routingKey, prompt, sessionType);
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
    int64_t chatId = 0;
    try { chatId = std::stoll(target.peer_id); } catch (...) {
        ALOG_WARNING("telegram", "Reply: invalid chat_id: " << target.peer_id);
        return;
    }

    auto chunks = SplitForTelegram(text, kTelegramMaxMsgLen);
    for (size_t i = 0; i < chunks.size(); ++i) {
        telegram::TelegramBotApi::SendMessageOptions opts;
        opts.chat_id = chatId;
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

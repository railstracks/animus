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
    std::cerr << "[telegram] Long Poll loop starting for " << rt->channel_name << std::endl;

    std::string token = GetString(rt->config, "access_token");
    if (token.empty()) {
        std::cerr << "[telegram] No access token for " << rt->channel_name << std::endl;
        rt->active = false;
        return;
    }

    telegram::TelegramBotApi api(m_ctx.httpClient);

    auto botInfo = api.GetMe(token);
    if (!botInfo) {
        std::cerr << "[telegram] getMe failed for " << rt->channel_name
                  << " — check bot token" << std::endl;
        rt->active = false;
        return;
    }
    std::cerr << "[telegram] Connected as @" << botInfo->username
              << " (" << botInfo->first_name << ", id=" << botInfo->id << ")" << std::endl;

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
            std::cerr << "[telegram] Exception for " << rt->channel_name
                      << ": " << ex.what() << std::endl;
            rt->consecutive_errors++;
            int backoff = std::min(60, rt->consecutive_errors * 5);
            rt->next_attempt = std::chrono::steady_clock::now() + std::chrono::seconds(backoff);
        }
    }

    std::cerr << "[telegram] Loop ended for " << rt->channel_name << std::endl;
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
        std::cerr << "[telegram] Bot added to chat " << chatId
                  << " (status: " << status << ")" << std::endl;
    } else if (status == "left" || status == "kicked") {
        std::cerr << "[telegram] Bot removed from chat " << chatId
                  << " (status: " << status << ")" << std::endl;
    }
}

void TelegramAdapter::SendReply(const ChannelReplyTarget& target, const std::string& text) {
    auto* rt = m_runtime.get();
    std::string token = GetString(rt->config, "access_token");
    if (token.empty()) {
        std::cerr << "[telegram] Reply: no access token for " << target.channel_name << std::endl;
        return;
    }

    telegram::TelegramBotApi api(m_ctx.httpClient);
    int64_t chatId = 0;
    try { chatId = std::stoll(target.peer_id); } catch (...) {
        std::cerr << "[telegram] Reply: invalid chat_id: " << target.peer_id << std::endl;
        return;
    }

    telegram::TelegramBotApi::SendMessageOptions opts;
    opts.chat_id = chatId;
    opts.text = text;

    auto msgId = api.SendMessage(token, opts);
    if (msgId) {
        std::cerr << "[telegram] Message sent to " << target.peer_id
                  << " (msg_id=" << *msgId << ")" << std::endl;
    } else {
        std::cerr << "[telegram] Send failed to " << target.peer_id << std::endl;
    }
}

} // namespace animus::kernel

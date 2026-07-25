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
#include "animus_kernel/whatsapp/WABinary.h"

#include <drogon/WebSocketClient.h>
#include <drogon/HttpRequest.h>
#include <trantor/net/EventLoop.h>

namespace animus::kernel {

using namespace channel_detail;

// ============================================================================
// EmailAdapter
// ============================================================================

void EmailAdapter::RunLoop() {
    WebSocketLoop();
}

void EmailAdapter::WebSocketLoop() {
    auto* rt = m_runtime.get();
    std::cerr << "[email] WebSocket loop starting for " << rt->channel_name << std::endl;

    std::string apiKey = GetString(rt->config, "api_key");
    std::string inboxId = GetString(rt->config, "inbox_id");

    if (apiKey.empty() || inboxId.empty()) {
        std::cerr << "[email] WS: missing api_key or inbox_id — falling back to polling" << std::endl;
        PollLoop();
        return;
    }

    trantor::EventLoop loop;
    auto wsPtr = drogon::WebSocketClient::newWebSocketClient("wss://ws.agentmail.to", &loop);

    wsPtr->setMessageHandler(
        [this, rt](std::string&& message,
                   const drogon::WebSocketClientPtr&,
                   const drogon::WebSocketMessageType& type) {
            if (type != drogon::WebSocketMessageType::Text) return;

            auto root = ParseJson(message);
            std::string eventType = GetString(root, "event_type");
            std::string msgType = GetString(root, "type");

            if (msgType == "subscribed") {
                std::cerr << "[email] WS: subscribed to "
                          << GetString(root, "event_types", "") << std::endl;
                return;
            }

            if (msgType == "error") {
                std::string errName = GetString(root, "name");
                std::string errMsg = GetString(root, "message");
                std::cerr << "[email] WS error: " << errName << ": " << errMsg << std::endl;
                if (errName == "authentication_error" || errName == "authorization_error" ||
                    errName == "invalid_api_key") {
                    rt->ws_connected = false;
                    rt->active = false;
                }
                return;
            }

            if (eventType == "message.received" || eventType == "message.received.spam" ||
                eventType == "message.received.blocked" || eventType == "message.received.unauthenticated") {
                std::string eventId = GetString(root, "event_id");
                if (!eventId.empty()) {
                    if (!rt->RememberEventStr(eventId)) return;
                }

                Json::Value msgObj = root["message"];
                std::string threadId = GetString(msgObj, "thread_id");
                std::string messageId = GetString(msgObj, "message_id");
                std::string sender = GetString(msgObj, "from");
                std::string subject = GetString(msgObj, "subject");
                std::string bodyText = GetString(msgObj, "text");

                if (bodyText.empty()) bodyText = GetString(msgObj, "extracted_text");
                if (bodyText.empty()) {
                    std::string htmlBody = GetString(msgObj, "html");
                    if (!htmlBody.empty()) bodyText = StripHtmlSimple(htmlBody);
                }

                rt->last_ws_event = std::chrono::steady_clock::now();

                if (eventType == "message.received") {
                    ProcessMessage(threadId, messageId, sender, subject, bodyText);
                }
            }
        });

    wsPtr->setConnectionClosedHandler(
        [rt](const drogon::WebSocketClientPtr&) {
            std::cerr << "[email] WS: connection closed for " << rt->channel_name << std::endl;
            rt->ws_connected = false;
        });

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setPath("/v0");
    req->setParameter("api_key", apiKey);

    wsPtr->connectToServer(req,
        [this, rt, wsPtr, &inboxId](drogon::ReqResult r,
                                    const drogon::HttpResponsePtr&,
                                    const drogon::WebSocketClientPtr&) {
            if (r != drogon::ReqResult::Ok) {
                rt->ws_connected = false;
                rt->consecutive_errors++;
                std::cerr << "[email] WS: connection failed (attempt "
                          << rt->consecutive_errors << ")" << std::endl;
                if (rt->consecutive_errors >= 5) {
                    wsPtr->stop();
                    return;
                }
                return;
            }

            rt->ws_connected = true;
            rt->consecutive_errors = 0;

            Json::Value subscribe;
            subscribe["type"] = "subscribe";
            subscribe["event_types"] = Json::Value(Json::arrayValue);
            subscribe["event_types"].append("message.received");
            subscribe["inbox_ids"] = Json::Value(Json::arrayValue);
            subscribe["inbox_ids"].append(inboxId);
            wsPtr->getConnection()->send(JsonCompact(subscribe));
            wsPtr->getConnection()->setPingMessage("", std::chrono::seconds(30));
        });

    rt->last_ws_event = std::chrono::steady_clock::now();

    loop.runEvery(5.0, [this, rt, &loop, wsPtr]() {
        if (!rt->active) {
            wsPtr->stop();
            loop.quit();
            return;
        }
        if (rt->ws_connected) {
            auto elapsed = std::chrono::steady_clock::now() - rt->last_ws_event;
            if (std::chrono::duration_cast<std::chrono::minutes>(elapsed).count() >= 5) {
                rt->consecutive_errors++;
                rt->last_ws_event = std::chrono::steady_clock::now();
                if (rt->consecutive_errors >= 3) {
                    rt->ws_connected = false;
                    wsPtr->stop();
                    rt->consecutive_errors = 0;
                }
            }
        }
    });

    loop.loop();

    if (rt->active && rt->consecutive_errors >= 5) {
        rt->ws_connected = false;
        rt->consecutive_errors = 0;
        PollLoop();
    }
}

void EmailAdapter::PollLoop() {
    auto* rt = m_runtime.get();
    std::cerr << "[email] Poll loop started for " << rt->channel_name << std::endl;

    while (rt->active) {
        auto now = std::chrono::steady_clock::now();
        if (now < rt->next_attempt) {
            std::this_thread::sleep_for(rt->next_attempt - now);
        }
        if (!rt->active) break;

        std::string apiKey = GetString(rt->config, "api_key");
        std::string inboxId = GetString(rt->config, "inbox_id");

        HttpClient::Request req;
        req.method = "GET";
        req.url = std::string("https://api.agentmail.to/v0/inboxes/") + inboxId + "/messages";
        req.headers["Authorization"] = "Bearer " + apiKey;

        auto resp = m_ctx.httpClient.Execute(req);
        if (resp.status_code != 200) {
            rt->consecutive_errors++;
            rt->next_attempt = now + std::chrono::seconds(30);
            continue;
        }

        auto json = ParseJson(resp.body);
        auto& messages = json["messages"];
        if (messages.isArray()) {
            for (const auto& msg : messages) {
                std::string messageId = GetString(msg, "message_id");
                if (!rt->RememberEventStr(messageId)) continue;

                std::string threadId = GetString(msg, "thread_id");
                std::string sender = GetString(msg, "from");
                std::string subject = GetString(msg, "subject");
                std::string bodyText = GetString(msg, "text");
                if (bodyText.empty()) bodyText = GetString(msg, "extracted_text");
                if (bodyText.empty()) {
                    std::string htmlBody = GetString(msg, "html");
                    if (!htmlBody.empty()) bodyText = StripHtmlSimple(htmlBody);
                }

                ProcessMessage(threadId, messageId, sender, subject, bodyText);
            }
        }

        rt->consecutive_errors = 0;
        rt->next_attempt = now + std::chrono::seconds(25);
    }
}

void EmailAdapter::ProcessMessage(const std::string& threadId,
                                   const std::string& messageId,
                                   const std::string& sender,
                                   const std::string& subject,
                                   const std::string& bodyText) {
    std::string routingKey = "thread:" + threadId;
    std::string sessionType = "email:chat";

    std::string prompt;
    if (!subject.empty()) {
        prompt = "New email from " + sender + " with subject '" + subject + "':\n\n" + bodyText;
    } else {
        prompt = "New email from " + sender + ":\n\n" + bodyText;
    }
    prompt += "\n\nMessage-ID: " + messageId;
    prompt += "\nThread-ID: " + threadId;
    prompt += "\n\nYou are responding via email. Use the email tool with action=reply and the message_id above to respond. Do NOT use the social tool to reply.";

    Dispatch(routingKey, prompt, sessionType);
}

void EmailAdapter::SendReply(const ChannelReplyTarget& target, const std::string& text) {
    auto* rt = m_runtime.get();
    std::string apiKey = GetString(rt->config, "api_key");
    std::string inboxId = target.email_inbox_id.empty()
        ? GetString(rt->config, "inbox_id")
        : target.email_inbox_id;
    if (apiKey.empty() || inboxId.empty()) {
        std::cerr << "[email] Reply: missing api_key or inbox_id" << std::endl;
        return;
    }

    Json::Value payload(Json::objectValue);
    payload["text"] = text;
    if (!target.email_thread_id.empty()) {
        payload["thread_id"] = target.email_thread_id;
    }

    HttpClient::Request req;
    req.method = "POST";
    req.url = std::string("https://api.agentmail.to/v0/inboxes/") + inboxId + "/messages/send";
    req.headers["Authorization"] = "Bearer " + apiKey;
    req.headers["Content-Type"] = "application/json";
    req.body = JsonCompact(payload);

    auto resp = m_ctx.httpClient.Execute(req);
    std::cerr << "[email] Reply: " << resp.status_code
              << " to thread=" << target.email_thread_id << std::endl;
}

// ============================================================================
// DiscordAdapter
// ============================================================================

void DiscordAdapter::RunLoop() {
    // Full implementation migrated from ChannelManager::DiscordGatewayLoop
    // This is a large method (~300 lines) — see original for full gateway protocol
    auto* rt = m_runtime.get();
    std::cerr << "[discord] Gateway loop starting for " << rt->channel_name << std::endl;

    // TODO: migrate full Discord Gateway WebSocket protocol
    // For now, this is a stub that will be filled in from the original
    std::cerr << "[discord] Gateway loop not yet migrated" << std::endl;
    rt->active = false;
}

void DiscordAdapter::ProcessMessage(const std::string& channelId,
                                     const std::string& messageId,
                                     const std::string& authorId,
                                     const std::string& authorUsername,
                                     const std::string& content) {
    std::string routingKey = "peer:" + channelId;
    std::string sessionType = "discord:chat";

    std::string prompt = "Discord message from " + authorUsername + ":\n" + content +
        "\n\nYou are responding via Discord. Respond naturally — your reply will be sent automatically. Do NOT use the social tool to reply.";

    Dispatch(routingKey, prompt, sessionType);
}

void DiscordAdapter::SendReply(const ChannelReplyTarget& target, const std::string& text) {
    auto* rt = m_runtime.get();
    std::string channelId;
    if (!target.peer_id.empty()) channelId = target.peer_id;
    else if (!target.post_id.empty()) channelId = target.post_id;

    std::string botToken = GetString(rt->config, "bot_token");
    if (botToken.empty()) {
        std::cerr << "[discord] SendReply: no bot_token" << std::endl;
        return;
    }

    std::string content = text;
    if (content.size() > 2000) content = content.substr(0, 1997) + "...";

    Json::Value body;
    body["content"] = content;

    HttpClient::Request req;
    req.method = "POST";
    req.url = "https://discord.com/api/v10/channels/" + channelId + "/messages";
    req.headers["Authorization"] = "Bot " + botToken;
    req.headers["Content-Type"] = "application/json";
    req.body = JsonCompact(body);
    req.follow_redirects = false;

    auto resp = m_ctx.httpClient.Execute(req);
    if (resp.status_code != 200 && resp.status_code != 201) {
        std::cerr << "[discord] SendReply failed (" << resp.status_code << ")" << std::endl;
    }
}

// ============================================================================
// WhatsAppAdapter
// ============================================================================

void WhatsAppAdapter::RunLoop() {
    auto* rt = m_runtime.get();
    std::cerr << "[whatsapp] Gateway loop starting for " << rt->channel_name << std::endl;

    // TODO: migrate full WhatsApp Gateway protocol
    // This involves baileys-like JS engine integration — large and complex
    std::cerr << "[whatsapp] Gateway loop not yet migrated" << std::endl;
    rt->active = false;
}

void WhatsAppAdapter::SendReply(const ChannelReplyTarget& target, const std::string& text) {
    std::cerr << "[whatsapp] SendReply to " << target.peer_id << std::endl;
    std::lock_guard<std::mutex> lock(m_outboxMutex);
    WhatsAppOutboundMessage msg;
    msg.baseJid = target.peer_id;
    msg.jid = target.peer_id;
    msg.text = text;
    msg.is_group = animus::whatsapp::isJidGroup(target.peer_id);
    m_outbox.push_back(std::move(msg));
}

std::string WhatsAppAdapter::GetQrUrl() const {
    // QR URL stored in runtime state
    if (!m_runtime) return "";
    std::lock_guard<std::mutex> lock(m_runtime->whatsapp_qr_mutex);
    return m_runtime->whatsapp_qr_url;
}

// ============================================================================
// SlackAdapter
// ============================================================================

void SlackAdapter::OnInit(const ChannelState& state) {
    std::string appToken = GetString(state.config, "app_token");
    m_useSocketMode = !appToken.empty();
}

void SlackAdapter::RunLoop() {
    if (m_useSocketMode) {
        SocketModeLoop();
    } else {
        PollingLoop();
    }
}

void SlackAdapter::SocketModeLoop() {
    auto* rt = m_runtime.get();
    std::cerr << "[slack-socket] Socket Mode loop starting for " << rt->channel_name << std::endl;

    // TODO: migrate full Slack Socket Mode WebSocket protocol
    std::cerr << "[slack-socket] Socket Mode not yet migrated" << std::endl;
    rt->active = false;
}

void SlackAdapter::PollingLoop() {
    auto* rt = m_runtime.get();
    std::cerr << "[slack] Polling loop starting for " << rt->channel_name << std::endl;

    // TODO: migrate full Slack REST polling loop
    std::cerr << "[slack] Polling not yet migrated" << std::endl;
    rt->active = false;
}

void SlackAdapter::SendReply(const ChannelReplyTarget& target, const std::string& text) {
    auto* rt = m_runtime.get();
    std::string channelId = target.peer_id.empty() ? target.post_id : target.peer_id;
    if (channelId.empty()) {
        std::cerr << "[slack] SendReply: no channel_id" << std::endl;
        return;
    }

    std::string botToken = GetString(rt->config, "bot_token");
    if (botToken.empty()) {
        std::cerr << "[slack] SendReply: no bot_token" << std::endl;
        return;
    }

    Json::Value body;
    body["channel"] = channelId;
    body["text"] = text;
    if (!target.reply_to_comment.empty()) {
        body["thread_ts"] = target.reply_to_comment;
    }

    HttpClient::Request req;
    req.method = "POST";
    req.url = "https://slack.com/api/chat.postMessage";
    req.headers["Authorization"] = "Bearer " + botToken;
    req.headers["Content-Type"] = "application/json; charset=utf-8";
    req.body = JsonCompact(body);
    req.follow_redirects = false;

    auto resp = m_ctx.httpClient.Execute(req);
    if (resp.status_code != 200) {
        std::cerr << "[slack] SendReply failed (" << resp.status_code << ")" << std::endl;
    } else {
        auto rdata = ParseJson(resp.body);
        if (rdata.isMember("ok") && rdata["ok"].asBool()) {
            std::cerr << "[slack] SendReply OK to " << channelId << std::endl;
        } else if (rdata.isMember("error")) {
            std::cerr << "[slack] SendReply API error: " << rdata["error"].asString() << std::endl;
        }
    }
}

// ============================================================================
// NextcloudAdapter
// ============================================================================

void NextcloudAdapter::RunLoop() {
    auto* rt = m_runtime.get();
    std::cerr << "[nextcloud] Poll loop starting for " << rt->channel_name << std::endl;

    // TODO: migrate full Nextcloud Talk OCS API polling
    std::cerr << "[nextcloud] Poll loop not yet migrated" << std::endl;
    rt->active = false;
}

void NextcloudAdapter::SendReply(const ChannelReplyTarget& target, const std::string& text) {
    auto* rt = m_runtime.get();
    std::string serverUrl = GetString(rt->config, "server_url");
    std::string username = GetString(rt->config, "username");
    std::string appPassword = GetString(rt->config, "app_password");

    if (serverUrl.empty() || username.empty() || appPassword.empty()) {
        std::cerr << "[nextcloud] SendReply: missing credentials" << std::endl;
        return;
    }

    Json::Value body;
    body["message"] = text;
    body["silent"] = false;

    HttpClient::Request req;
    req.method = "POST";
    req.url = serverUrl + "/ocs/v2.php/apps/spreed/api/v1/chat/" + target.peer_id;
    req.headers["Content-Type"] = "application/json";
    req.headers["Accept"] = "application/json";
    req.headers["OCS-APIREQUEST"] = "true";
    req.headers["Authorization"] = "Basic " + Base64EncodeStr(username + ":" + appPassword);
    req.body = JsonCompact(body);
    req.follow_redirects = false;

    auto resp = m_ctx.httpClient.Execute(req);
    if (resp.status_code != 200 && resp.status_code != 201) {
        std::cerr << "[nextcloud] SendReply failed (" << resp.status_code << ")" << std::endl;
    } else {
        std::cerr << "[nextcloud] SendReply OK to " << target.peer_id << std::endl;
    }
}

} // namespace animus::kernel

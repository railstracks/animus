#include "animus_kernel/Log.h"
#include <unordered_map>
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
// EmailAdapter — full implementation
// ============================================================================

void EmailAdapter::RunLoop() {
    WebSocketLoop();
}

void EmailAdapter::WebSocketLoop() {
    auto* rt = m_runtime.get();
    ALOG_DEBUG("email", "WebSocket loop starting for " << rt->channel_name);

    std::string apiKey = GetString(rt->config, "api_key");
    std::string inboxId = GetString(rt->config, "inbox_id");

    if (apiKey.empty() || inboxId.empty()) {
        ALOG_WARNING("email", "WS: missing api_key or inbox_id — falling back to polling");
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
                ALOG_DEBUG("email", "WS: subscribed");
                return;
            }

            if (msgType == "error") {
                std::string errName = GetString(root, "name");
                ALOG_ERROR("email", "WS error: " << errName
                          << ": " << GetString(root, "message"));
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
                if (!eventId.empty() && !rt->RememberEventStr(eventId)) return;

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
            ALOG_DEBUG("email", "WS: closed for " << rt->channel_name);
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
                if (rt->consecutive_errors >= 5) { wsPtr->stop(); return; }
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
        if (!rt->active) { wsPtr->stop(); loop.quit(); return; }
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
    ALOG_INFO("email", "Poll loop started for " << rt->channel_name);

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

                std::string bodyText = GetString(msg, "text");
                if (bodyText.empty()) bodyText = GetString(msg, "extracted_text");
                if (bodyText.empty()) {
                    std::string htmlBody = GetString(msg, "html");
                    if (!htmlBody.empty()) bodyText = StripHtmlSimple(htmlBody);
                }

                ProcessMessage(GetString(msg, "thread_id"), messageId,
                               GetString(msg, "from"), GetString(msg, "subject"), bodyText);
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
    prompt += "\n\nYou are responding via email. Use the email tool with action=reply and the message_id above to respond.";

    Dispatch(routingKey, prompt, sessionType);
}

void EmailAdapter::SendReply(const ChannelReplyTarget& target, const std::string& text) {
    auto* rt = m_runtime.get();
    std::string apiKey = GetString(rt->config, "api_key");
    std::string inboxId = target.email_inbox_id.empty()
        ? GetString(rt->config, "inbox_id") : target.email_inbox_id;
    if (apiKey.empty() || inboxId.empty()) return;

    Json::Value payload(Json::objectValue);
    payload["text"] = text;
    if (!target.email_thread_id.empty()) payload["thread_id"] = target.email_thread_id;

    HttpClient::Request req;
    req.method = "POST";
    req.url = std::string("https://api.agentmail.to/v0/inboxes/") + inboxId + "/messages/send";
    req.headers["Authorization"] = "Bearer " + apiKey;
    req.headers["Content-Type"] = "application/json";
    req.body = JsonCompact(payload);

    auto resp = m_ctx.httpClient.Execute(req);
    ALOG_DEBUG("email", "Reply: " << resp.status_code
              << " thread=" << target.email_thread_id);
}

// ============================================================================
// DiscordAdapter — SendReply + ProcessMessage (Gateway loop stays in
// DiscordGatewayLoop.cpp as ChannelManager method for now, as it's 616 lines
// of WebSocket protocol code with deep ChannelManager coupling)
// ============================================================================

void DiscordAdapter::RunLoop() {
    // Discord Gateway loop remains in DiscordGatewayLoop.cpp as a
    // ChannelManager method. It will be migrated to this adapter in a
    // future refactor. For now, this adapter handles SendReply only.
    auto* rt = m_runtime.get();
    ALOG_DEBUG("discord", "Gateway loop is handled by ChannelManager for now. "
              << "SendReply is handled by DiscordAdapter.");
    // Keep the thread alive
    while (rt->active && !m_stopRequested) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void DiscordAdapter::ProcessMessage(const std::string& channelId,
                                     const std::string& messageId,
                                     const std::string& authorId,
                                     const std::string& authorUsername,
                                     const std::string& content) {
    std::string routingKey = "peer:" + channelId;
    std::string sessionType = "discord:chat";

    std::string prompt = "Discord message from " + authorUsername + ":\n" + content +
        "\n\nYou are responding via Discord. Respond naturally — your reply will be sent automatically.";

    Dispatch(routingKey, prompt, sessionType);
}

void DiscordAdapter::SendReply(const ChannelReplyTarget& target, const std::string& text) {
    auto* rt = m_runtime.get();
    std::string channelId = !target.peer_id.empty() ? target.peer_id : target.post_id;
    std::string botToken = GetString(rt->config, "bot_token");
    if (botToken.empty() || channelId.empty()) return;

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
        ALOG_WARNING("discord", "SendReply failed (" << resp.status_code << ")");
    }
}

// ============================================================================
// WhatsAppAdapter — SendReply + QR (Gateway loop stays in
// WhatsAppGatewayLoop.cpp as ChannelManager method, 1547 lines)
// ============================================================================

void WhatsAppAdapter::RunLoop() {
    auto* rt = m_runtime.get();
    ALOG_DEBUG("whatsapp", "Gateway loop is handled by ChannelManager for now.");
    while (rt->active && !m_stopRequested) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void WhatsAppAdapter::SendReply(const ChannelReplyTarget& target, const std::string& text) {
    std::lock_guard<std::mutex> lock(m_outboxMutex);
    WhatsAppOutboundMessage msg;
    msg.baseJid = target.peer_id;
    msg.jid = target.peer_id;
    msg.text = text;
    msg.is_group = animus::whatsapp::isJidGroup(target.peer_id);
    m_outbox.push_back(std::move(msg));
}

std::string WhatsAppAdapter::GetQrUrl() const {
    if (!m_runtime) return "";
    std::lock_guard<std::mutex> lock(m_runtime->whatsapp_qr_mutex);
    return m_runtime->whatsapp_qr_url;
}

// ============================================================================
// SlackAdapter — full Socket Mode + Polling implementation
// ============================================================================

void SlackAdapter::OnInit(const ChannelState& state) {
    m_useSocketMode = !GetString(state.config, "app_token").empty();
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
    ALOG_DEBUG("slack-socket", "Socket Mode loop starting for " << rt->channel_name);

    const std::string appToken = GetString(rt->config, "app_token");
    const std::string botToken = GetString(rt->config, "bot_token");

    // Resolve bot user ID for self-filtering
    std::string botUserId = GetString(rt->config, "bot_user_id");
    if (botUserId.empty() && !botToken.empty()) {
        HttpClient::Request authReq;
        authReq.method = "POST";
        authReq.url = "https://slack.com/api/auth.test";
        authReq.headers["Authorization"] = "Bearer " + botToken;
        authReq.headers["Content-Type"] = "application/x-www-form-urlencoded";
        authReq.body = "";
        authReq.follow_redirects = false;

        auto authResp = m_ctx.httpClient.Execute(authReq);
        if (authResp.status_code == 200) {
            auto data = ParseJson(authResp.body);
            if (data.isMember("ok") && data["ok"].asBool()) {
                botUserId = data["user_id"].asString();
                ALOG_DEBUG("slack-socket", "Resolved bot user ID: " << botUserId);
            }
        }
    }

    // Display-name resolution (#42): Slack ids are stable but unreadable.
    // Resolve once per connection and cache (Discord GUILD_CREATE precedent).
    std::unordered_map<std::string, std::string> userNameCache;
    std::unordered_map<std::string, std::string> channelNameCache;
    auto apiFormPost = [this, botToken](const std::string& method,
                                        const std::string& body) -> Json::Value {
        HttpClient::Request req;
        req.method = "POST";
        req.url = "https://slack.com/api/" + method;
        req.headers["Authorization"] = "Bearer " + botToken;
        req.headers["Content-Type"] = "application/x-www-form-urlencoded";
        req.body = body;
        req.follow_redirects = false;
        auto resp = m_ctx.httpClient.Execute(req);
        if (resp.status_code != 200) return Json::Value();
        return ParseJson(resp.body);
    };
    auto resolveUser = [&](const std::string& uid) -> std::string {
        auto it = userNameCache.find(uid);
        if (it != userNameCache.end()) return it->second;
        std::string name = uid;
        auto data = apiFormPost("users.info", "user=" + uid);
        if (data.isObject() && data.get("ok", false).asBool()
            && data["user"].isObject()) {
            const auto& prof = data["user"]["profile"];
            name = GetString(prof, "display_name");
            if (name.empty()) name = GetString(prof, "real_name");
            if (name.empty()) name = GetString(data["user"], "name");
            if (name.empty()) name = uid;
        }
        userNameCache[uid] = name;
        return name;
    };
    auto resolveChannel = [&](const std::string& cid) -> std::string {
        auto it = channelNameCache.find(cid);
        if (it != channelNameCache.end()) return it->second;
        std::string name = cid;
        auto data = apiFormPost("conversations.info", "channel=" + cid);
        if (data.isObject() && data.get("ok", false).asBool()
            && data["channel"].isObject()) {
            name = GetString(data["channel"], "name");
            if (name.empty()) name = cid;
        }
        channelNameCache[cid] = name;
        return name;
    };

    while (rt->active && !m_stopRequested) {
        // Step 1: Get WebSocket URL
        HttpClient::Request connReq;
        connReq.method = "POST";
        connReq.url = "https://slack.com/api/apps.connections.open";
        connReq.headers["Authorization"] = "Bearer " + appToken;
        connReq.headers["Content-Type"] = "application/x-www-form-urlencoded";
        connReq.body = "";
        connReq.follow_redirects = false;

        auto connResp = m_ctx.httpClient.Execute(connReq);
        if (connResp.status_code != 200) {
            ALOG_WARNING("slack-socket", "apps.connections.open failed (HTTP "
                      << connResp.status_code << ")");
            for (int i = 0; i < 30 && rt->active; ++i)
                std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        auto connData = ParseJson(connResp.body);
        if (!connData.isMember("ok") || !connData["ok"].asBool() || !connData.isMember("url")) {
            ALOG_DEBUG("slack-socket", "Bad response from apps.connections.open");
            for (int i = 0; i < 30 && rt->active; ++i)
                std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        std::string wsUrl = connData["url"].asString();

        // Parse wss:// URL into host + path
        std::string wsHost, wsPath;
        {
            size_t hostStart = wsUrl.find("://");
            if (hostStart != std::string::npos) {
                hostStart += 3;
                size_t pathStart = wsUrl.find("/", hostStart);
                if (pathStart != std::string::npos) {
                    wsHost = wsUrl.substr(hostStart, pathStart - hostStart);
                    wsPath = wsUrl.substr(pathStart);
                } else {
                    wsHost = wsUrl.substr(hostStart);
                    wsPath = "/";
                }
            }
        }

        std::string wsConnectUrl = "wss://" + wsHost;

        // Step 2: Connect WebSocket
        trantor::EventLoop loop;
        auto wsPtr = drogon::WebSocketClient::newWebSocketClient(wsConnectUrl, &loop);

        // Liveness bookkeeping: Slack silently drops Socket Mode connections
        // after hours; without a watchdog we sit on a dead socket forever
        // (Sep 2 incident: socket deaf for 7.5h, zero errors logged).
        auto lastFrame = std::chrono::steady_clock::now();
        const auto attemptStart = lastFrame;

        wsPtr->setMessageHandler(
            [this, rt, &botUserId, wsPtr, &resolveUser, &resolveChannel,
             &loop, &lastFrame](std::string&& message,
                        const drogon::WebSocketClientPtr&,
                        const drogon::WebSocketMessageType& type) {
                lastFrame = std::chrono::steady_clock::now();
                if (type == drogon::WebSocketMessageType::Ping ||
                    type == drogon::WebSocketMessageType::Pong) return;

                if (type != drogon::WebSocketMessageType::Text &&
                    type != drogon::WebSocketMessageType::Binary) return;

                auto envelope = ParseJson(message);
                std::string msgType = GetString(envelope, "type");
                std::string envelopeId = GetString(envelope, "envelope_id");

                if (msgType == "hello") {
                    ALOG_INFO("slack-socket", "Connected (hello)");
                    rt->ws_connected = true;
                    rt->consecutive_errors = 0;
                    return;
                }

                if (msgType == "disconnect") {
                    ALOG_DEBUG("slack-socket", "Disconnect: " << GetString(envelope, "reason"));
                    rt->ws_connected = false;
                    wsPtr->stop();
                    loop.quit();
                    return;
                }

                // Acknowledge
                if (!envelopeId.empty()) {
                    Json::Value ack;
                    ack["envelope_id"] = envelopeId;
                    wsPtr->getConnection()->send(JsonCompact(ack));
                }

                // Process events_api
                if (msgType == "events_api") {
                    Json::Value event = envelope["payload"]["event"];
                    if (!event.isObject()) return;

                    std::string eventType = GetString(event, "type");
                    if (eventType != "message") return;

                    // Skip subtypes and bot messages
                    if (event.isMember("subtype")) return;
                    std::string botId = GetString(event, "bot_id");
                    if (!botId.empty()) return;

                    std::string userId = GetString(event, "user");
                    if (userId == botUserId) return;

                    std::string text = GetString(event, "text");
                    std::string channel = GetString(event, "channel");
                    std::string ts = GetString(event, "ts");
                    std::string threadTs = GetString(event, "thread_ts");

                    if (text.empty() || channel.empty() || ts.empty()) return;

                    // Mention filtering
                    bool respondToAll = GetString(rt->config, "respond_to_all_messages") == "true";
                    bool respondToMentions = GetString(rt->config, "respond_to_mentions") != "false";
                    bool isMention = !botUserId.empty() &&
                        text.find("<@" + botUserId + ">") != std::string::npos;

                    if (!respondToAll && !isMention && respondToMentions) return;

                    // Strip mention
                    std::string cleanText = text;
                    if (!botUserId.empty()) {
                        std::string mentionTag = "<@" + botUserId + ">";
                        size_t pos;
                        while ((pos = cleanText.find(mentionTag)) != std::string::npos)
                            cleanText.erase(pos, mentionTag.size());
                        while (!cleanText.empty() && (cleanText[0] == ' ' || cleanText[0] == '\n'))
                            cleanText.erase(0, 1);
                        while (!cleanText.empty() && (cleanText.back() == ' ' || cleanText.back() == '\n'))
                            cleanText.pop_back();
                    }

                    bool threaded = (GetString(rt->config, "threaded_replies") == "true");
                    std::string routingKey = "chat:slack:" + channel;
                    if (!threadTs.empty() && threadTs != ts) {
                        routingKey = "chat:slack:" + threadTs;
                    } else if (threaded && threadTs.empty()) {
                        routingKey = "chat:slack:" + ts;
                    }

                    // Context card (#15/#42) — tool-only delivery: channels
                    // may be busy, so silence must be a first-class outcome.
                    const bool isDm = !channel.empty() && channel[0] == 'D';
                    const bool inThread = !threadTs.empty() && threadTs != ts;
                    const std::string userName = resolveUser(userId);
                    const std::string channelName = resolveChannel(channel);

                    std::string displayText;
                    if (isDm) {
                        displayText = "private message from " + userName
                            + " (user:" + userId + "):\n" + cleanText;
                    } else {
                        displayText = "Slack message from " + userName
                            + " (user:" + userId + ") in #" + channelName;
                        if (inThread) displayText += " (in thread)";
                        displayText += ":\n" + cleanText;
                    }

                    Json::Value meta;
                    meta["message_type"] = "chat";
                    meta["delivery"] = "tool";
                    Json::Value origin;
                    origin["user_display"] = userName;
                    origin["user_id"] = userId;
                    if (!isDm) {
                        origin["channel"] = channelName;
                        origin["channel_id"] = channel;
                    }
                    meta["origin"] = origin;
                    meta["channel_id"] = channel;
                    meta["source_message_id"] = ts;
                    // Thread targeting is flag-authoritative
                    // (threaded_replies): in-thread arrivals always reply in
                    // their thread; top-level arrivals thread ONLY when the
                    // adapter is configured for threaded replies — otherwise
                    // the reply is a top-level channel response.
                    if (inThread) {
                        meta["reply_parent_id"] = threadTs;
                        meta["thread_root_id"] = threadTs;
                    } else if (threaded) {
                        meta["reply_parent_id"] = ts;
                        meta["thread_root_id"] = ts;
                    }
                    std::string instructions =
                        "Reply using the channels tool with action=reply; "
                        "channel_id and thread_ts are provided by the arrival "
                        "and filled automatically. Text replies are NOT "
                        "delivered. In channels, if the message is not "
                        "addressed to you, staying silent is correct.";
                    if (!isDm) {
                        instructions += threaded
                            ? " Replies are posted as threaded replies to the "
                              "original message."
                            : " Replies are posted as TOP-LEVEL responses in "
                              "the channel: send the reply WITHOUT a thread_ts "
                              "parameter and never set thread_ts yourself "
                              "(unless the arrival is itself inside a thread, "
                              "in which case reply in that thread).";
                    }
                    meta["reply_instructions"] = instructions;
                    Json::StreamWriterBuilder wb;
                    wb["indentation"] = "";
                    std::string metadata = Json::writeString(wb, meta);

                    ChannelReplyTarget replyTarget;
                    replyTarget.channel_name = rt->channel_name;
                    replyTarget.channel_type = rt->channel_type;
                    replyTarget.type = ChannelReplyTarget::Chat;
                    replyTarget.peer_id = channel;
                    if (!threadTs.empty() && threadTs != ts) {
                        replyTarget.reply_to_comment = threadTs;
                    } else if (threaded && threadTs.empty()) {
                        replyTarget.reply_to_comment = ts;
                    }

                    m_ctx.dispatch(rt->agent_id, routingKey, displayText, "slack", replyTarget, metadata);

                    ALOG_DEBUG("slack-socket", "Dispatched from " << userId
                              << " in " << channel << " ts=" << ts);
                }
            });

        wsPtr->setConnectionClosedHandler(
            [rt, &loop](const drogon::WebSocketClientPtr&) {
                ALOG_DEBUG("slack-socket", "WebSocket closed for " << rt->channel_name);
                rt->ws_connected = false;
                loop.quit();
            });

        auto req = drogon::HttpRequest::newHttpRequest();
        req->setPath(wsPath);

        wsPtr->connectToServer(req,
            [rt, &loop](drogon::ReqResult r, const drogon::HttpResponsePtr&,
                 const drogon::WebSocketClientPtr&) {
                if (r != drogon::ReqResult::Ok) {
                    rt->ws_connected = false;
                    rt->consecutive_errors++;
                    loop.quit();
                }
            });

        loop.runEvery(10.0, [rt, &loop, wsPtr, &lastFrame, &attemptStart]() {
            if (!rt->active) { wsPtr->stop(); loop.quit(); return; }
            auto now = std::chrono::steady_clock::now();
            auto idleS = std::chrono::duration_cast<std::chrono::seconds>(
                now - lastFrame).count();
            auto attemptS = std::chrono::duration_cast<std::chrono::seconds>(
                now - attemptStart).count();
            // Connected but silent past Slack's ping cadence: dead socket.
            if (rt->ws_connected && idleS > 180) {
                ALOG_WARNING("slack-socket", "Connection silent for "
                             << idleS << "s - forcing reconnect");
                rt->ws_connected = false;
                wsPtr->stop();
                loop.quit();
            } else if (!rt->ws_connected && attemptS > 60) {
                // Handshake never completed: retry the whole cycle.
                ALOG_WARNING("slack-socket", "Handshake stalled for "
                             << attemptS << "s - retrying");
                wsPtr->stop();
                loop.quit();
            }
        });

        loop.loop();
        rt->ws_connected = false;

        if (!rt->active) break;

        ALOG_DEBUG("slack-socket", "Reconnecting in 5s...");
        for (int i = 0; i < 5 && rt->active; ++i)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    ALOG_INFO("slack-socket", "Socket Mode loop stopped for " << rt->channel_name);
}

void SlackAdapter::PollingLoop() {
    auto* rt = m_runtime.get();
    ALOG_INFO("slack", "Polling loop started for " << rt->channel_name);

    const std::string botToken = GetString(rt->config, "bot_token");
    if (botToken.empty()) {
        ALOG_WARNING("slack", "No bot_token for " << rt->channel_name);
        return;
    }

    // Resolve bot user ID
    std::string botUserId = GetString(rt->config, "bot_user_id");
    if (botUserId.empty()) {
        HttpClient::Request req;
        req.method = "POST";
        req.url = "https://slack.com/api/auth.test";
        req.headers["Authorization"] = "Bearer " + botToken;
        req.headers["Content-Type"] = "application/x-www-form-urlencoded";
        req.body = "";
        req.follow_redirects = false;

        auto resp = m_ctx.httpClient.Execute(req);
        if (resp.status_code == 200) {
            auto data = ParseJson(resp.body);
            if (data.isMember("ok") && data["ok"].asBool()) {
                botUserId = data["user_id"].asString();
            }
        }
    }

    // Discover channels
    std::vector<std::string> monitoredChannels;
    std::string channelsJson = GetString(rt->config, "monitored_channels");
    if (!channelsJson.empty()) {
        auto arr = ParseJson(channelsJson);
        if (arr.isArray()) {
            for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
                monitoredChannels.push_back(arr[i].asString());
        }
    } else {
        HttpClient::Request req;
        req.method = "GET";
        req.url = "https://slack.com/api/conversations.list?types=public_channel,private_channel&limit=1000";
        req.headers["Authorization"] = "Bearer " + botToken;
        req.follow_redirects = false;

        auto resp = m_ctx.httpClient.Execute(req);
        if (resp.status_code == 200) {
            auto data = ParseJson(resp.body);
            if (data.isMember("ok") && data["ok"].asBool() && data["channels"].isArray()) {
                for (const auto& ch : data["channels"]) {
                    if (ch.isMember("is_member") && ch["is_member"].asBool())
                        monitoredChannels.push_back(ch["id"].asString());
                }
            }
        }
        ALOG_DEBUG("slack", "Auto-discovered " << monitoredChannels.size() << " channels");
    }

    if (monitoredChannels.empty()) {
        ALOG_WARNING("slack", "No channels found. Idle.");
        while (rt->active) std::this_thread::sleep_for(std::chrono::seconds(30));
        return;
    }

    std::string latestTs = rt->lp_ts;
    const auto pollInterval = std::chrono::seconds(10);

    while (rt->active && !m_stopRequested) {
        rt->next_attempt = std::chrono::steady_clock::now() + pollInterval;

        for (const auto& channelId : monitoredChannels) {
            if (!rt->active) break;

            try {
                std::string url = "https://slack.com/api/conversations.history?channel="
                    + channelId + "&limit=5";
                if (!latestTs.empty()) url += "&oldest=" + latestTs;

                HttpClient::Request req;
                req.method = "GET";
                req.url = url;
                req.headers["Authorization"] = "Bearer " + botToken;
                req.follow_redirects = false;

                auto resp = m_ctx.httpClient.Execute(req);

                if (resp.status_code == 429) {
                    std::string retryAfter = resp.headers.count("retry-after")
                        ? resp.headers.at("retry-after") : "10";
                    std::this_thread::sleep_for(std::chrono::seconds(std::stoi(retryAfter)));
                    continue;
                }

                if (resp.status_code != 200) continue;

                auto data = ParseJson(resp.body);
                if (!data.isMember("ok") || !data["ok"].asBool()) continue;
                if (!data["messages"].isArray()) continue;

                for (int i = static_cast<int>(data["messages"].size()) - 1; i >= 0; --i) {
                    const auto& msg = data["messages"][i];
                    if (!msg.isObject()) continue;

                    std::string userId = GetString(msg, "user");
                    if (userId == botUserId) continue;
                    std::string botId = GetString(msg, "bot_id");
                    if (!botId.empty()) continue;
                    if (msg.isMember("subtype")) continue;

                    std::string text = GetString(msg, "text");
                    std::string ts = GetString(msg, "ts");
                    std::string threadTs = GetString(msg, "thread_ts");
                    if (text.empty() || ts.empty()) continue;

                    bool respondToAll = GetString(rt->config, "respond_to_all_messages") == "true";
                    bool respondToMentions = GetString(rt->config, "respond_to_mentions") != "false";
                    bool isMention = text.find("<@" + botUserId + ">") != std::string::npos;
                    if (!respondToAll && !isMention && respondToMentions) continue;
                    if (!respondToAll && !respondToMentions) continue;

                    // Strip mention
                    std::string cleanText = text;
                    std::string mentionTag = "<@" + botUserId + ">";
                    size_t pos;
                    while ((pos = cleanText.find(mentionTag)) != std::string::npos)
                        cleanText.erase(pos, mentionTag.size());
                    while (!cleanText.empty() && (cleanText[0] == ' ' || cleanText[0] == '\n'))
                        cleanText.erase(0, 1);
                    while (!cleanText.empty() && (cleanText.back() == ' ' || cleanText.back() == '\n'))
                        cleanText.pop_back();

                    bool threaded = (GetString(rt->config, "threaded_replies") == "true");
                    std::string routingKey = "chat:slack:" + channelId;
                    if (!threadTs.empty() && threadTs != ts) {
                        routingKey = "chat:slack:" + threadTs;
                    } else if (threaded && threadTs.empty()) {
                        routingKey = "chat:slack:" + ts;
                    }

                    std::string prompt = "[Slack message from <" + userId + ">";
                    prompt += " in channel " + channelId;
                    if (!threadTs.empty() && threadTs != ts)
                        prompt += " (thread " + threadTs + ")";
                    prompt += "]\n" + cleanText;

                    ChannelReplyTarget replyTarget;
                    replyTarget.channel_name = rt->channel_name;
                    replyTarget.channel_type = rt->channel_type;
                    replyTarget.type = ChannelReplyTarget::Chat;
                    replyTarget.peer_id = channelId;
                    if (!threadTs.empty() && threadTs != ts) {
                        replyTarget.reply_to_comment = threadTs;
                    } else if (threaded && threadTs.empty()) {
                        replyTarget.reply_to_comment = ts;
                    }

                    m_ctx.dispatch(rt->agent_id, routingKey, prompt, "slack", replyTarget, "{}");
                    latestTs = ts;
                }
            } catch (const std::exception& e) {
                ALOG_ERROR("slack", "Exception: " << e.what());
            }
        }

        if (m_ctx.configStore && !latestTs.empty()) {
            m_ctx.configStore->Set("", "channel." + rt->channel_name + ".polling.latest_ts", latestTs);
        }

        while (rt->active && std::chrono::steady_clock::now() < rt->next_attempt)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    ALOG_INFO("slack", "Polling loop stopped for " << rt->channel_name);
}

void SlackAdapter::SendReply(const ChannelReplyTarget& target, const std::string& text) {
    auto* rt = m_runtime.get();
    std::string channelId = target.peer_id.empty() ? target.post_id : target.peer_id;
    if (channelId.empty()) return;

    std::string botToken = GetString(rt->config, "bot_token");
    if (botToken.empty()) return;

    Json::Value body;
    body["channel"] = channelId;
    body["text"] = text;
    if (!target.reply_to_comment.empty()) body["thread_ts"] = target.reply_to_comment;

    HttpClient::Request req;
    req.method = "POST";
    req.url = "https://slack.com/api/chat.postMessage";
    req.headers["Authorization"] = "Bearer " + botToken;
    req.headers["Content-Type"] = "application/json; charset=utf-8";
    req.body = JsonCompact(body);
    req.follow_redirects = false;

    auto resp = m_ctx.httpClient.Execute(req);
    if (resp.status_code != 200) {
        ALOG_WARNING("slack", "SendReply failed (" << resp.status_code << ")");
    } else {
        auto rdata = ParseJson(resp.body);
        if (rdata.isMember("ok") && rdata["ok"].asBool()) {
            ALOG_DEBUG("slack", "SendReply OK to " << channelId);
        } else if (rdata.isMember("error")) {
            ALOG_ERROR("slack", "SendReply API error: " << rdata["error"].asString());
        }
    }
}

// ============================================================================
// NextcloudAdapter — full polling implementation
// ============================================================================

void NextcloudAdapter::RunLoop() {
    auto* rt = m_runtime.get();
    ALOG_DEBUG("nextcloud", "Talk poll loop starting for " << rt->channel_name);

    std::string serverUrl = GetString(rt->config, "server_url");
    std::string username = GetString(rt->config, "username");
    std::string appPassword = GetString(rt->config, "app_password");

    if (serverUrl.empty() || username.empty() || appPassword.empty()) {
        ALOG_WARNING("nextcloud", "Missing credentials for " << rt->channel_name);
        rt->active = false;
        return;
    }

    while (!serverUrl.empty() && serverUrl.back() == '/') serverUrl.pop_back();

    std::string authHeader = "Basic " + Base64EncodeStr(username + ":" + appPassword);
    std::string apiBase = serverUrl + "/ocs/v2.php/apps/spreed/api/v4";
    std::string chatApiBase = serverUrl + "/ocs/v2.php/apps/spreed/api/v1";

    bool respondInDm = GetString(rt->config, "respond_in_dm", "true") != "false";
    bool respondInGroupOnMention = GetString(rt->config, "respond_in_group_on_mention", "true") != "false";
    std::string mentionTrigger = GetString(rt->config, "group_mention_trigger", "@" + username);

    // Parse watch_tokens
    std::vector<std::string> watchTokens;
    std::string watchTokensJson = GetString(rt->config, "watch_tokens");
    if (!watchTokensJson.empty()) {
        auto arr = ParseJson(watchTokensJson);
        if (arr.isArray()) {
            for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
                if (arr[i].isString()) watchTokens.push_back(arr[i].asString());
        }
    }

    auto isDm = [](int convType) { return convType == 1 || convType == 6; };

    struct ConvState {
        int lastMsgId{0};
        int type{2};
        std::string displayName;
    };
    std::unordered_map<std::string, ConvState> conversations;

    auto syncConversationList = [&]() {
        HttpClient::Request req;
        req.method = "GET";
        req.url = apiBase + "/room?format=json";
        req.headers["Accept"] = "application/json";
        req.headers["OCS-APIREQUEST"] = "true";
        req.headers["Authorization"] = authHeader;
        req.follow_redirects = false;

        auto resp = m_ctx.httpClient.Execute(req);
        if (resp.status_code != 200) return;

        auto data = ParseJson(resp.body);
        if (!data.isMember("ocs") || !data["ocs"]["data"].isArray()) return;

        for (const auto& room : data["ocs"]["data"]) {
            std::string token = GetString(room, "token");
            if (token.empty()) continue;

            int convType = room.isMember("type") ? room["type"].asInt() : 2;
            if (convType == 4) continue; // changelog

            if (!watchTokens.empty()) {
                bool found = false;
                for (const auto& wt : watchTokens) if (wt == token) { found = true; break; }
                if (!found) continue;
            }

            std::string displayName = GetString(room, "displayName");
            if (displayName.empty()) displayName = token;

            if (conversations.find(token) == conversations.end()) {
                ConvState cs;
                cs.type = convType;
                cs.displayName = displayName;
                if (m_ctx.configStore) {
                    std::string stored = m_ctx.configStore->Get("",
                        "channel." + rt->channel_name + ".polling." + token + ".last_msg_id");
                    if (!stored.empty()) {
                        try { cs.lastMsgId = std::stoi(stored); } catch (...) {}
                    }
                }
                conversations[token] = cs;
                ALOG_DEBUG("nextcloud", "Watching: " << displayName
                          << " (token=" << token << ", type=" << convType << ")");
            } else {
                conversations[token].type = convType;
                conversations[token].displayName = displayName;
            }
        }
    };

    syncConversationList();
    auto lastRoomSync = std::chrono::steady_clock::now();
    const auto roomSyncInterval = std::chrono::seconds(60);

    while (rt->active && !m_stopRequested) {
        try {
            auto now = std::chrono::steady_clock::now();

            if (now - lastRoomSync > roomSyncInterval) {
                syncConversationList();
                lastRoomSync = now;
            }

            if (conversations.empty()) {
                std::this_thread::sleep_for(std::chrono::seconds(10));
                continue;
            }

            for (auto& [token, conv] : conversations) {
                if (!rt->active) break;

                std::string url = chatApiBase + "/chat/" + token +
                    "?lookIntoFuture=1&timeout=30&format=json";
                if (conv.lastMsgId > 0)
                    url += "&lastKnownMessageId=" + std::to_string(conv.lastMsgId);

                HttpClient::Request req;
                req.method = "GET";
                req.url = url;
                req.headers["Accept"] = "application/json";
                req.headers["OCS-APIREQUEST"] = "true";
                req.headers["Authorization"] = authHeader;
                req.follow_redirects = false;
                req.timeout_seconds = 35;

                auto resp = m_ctx.httpClient.Execute(req);
                if (resp.status_code == 304) continue;
                if (resp.status_code != 200) continue;

                auto data = ParseJson(resp.body);
                if (!data.isMember("ocs") || !data["ocs"]["data"].isArray()) continue;

                for (const auto& msg : data["ocs"]["data"]) {
                    int msgId = msg.isMember("id") ? msg["id"].asInt() : 0;
                    if (msgId <= conv.lastMsgId) continue;
                    conv.lastMsgId = std::max(conv.lastMsgId, msgId);

                    if (m_ctx.configStore) {
                        m_ctx.configStore->Set("",
                            "channel." + rt->channel_name + ".polling." + token + ".last_msg_id",
                            std::to_string(conv.lastMsgId));
                    }

                    // Skip system/bot/own messages
                    std::string systemMsg = GetString(msg, "systemMessage");
                    if (!systemMsg.empty()) continue;
                    std::string actorType = GetString(msg, "actorType");
                    if (actorType == "bots") continue;
                    std::string actorId = GetString(msg, "actorId");
                    if (actorId == username) continue;

                    std::string messageText = GetString(msg, "message");
                    if (messageText.empty()) continue;

                    std::string actorName = GetString(msg, "actorDisplayName");
                    if (actorName.empty()) actorName = actorId;

                    bool shouldDispatch = false;
                    if (isDm(conv.type) && respondInDm) {
                        shouldDispatch = true;
                    } else if (!isDm(conv.type) && respondInGroupOnMention) {
                        if (messageText.find(mentionTrigger) != std::string::npos)
                            shouldDispatch = true;
                    } else if (!isDm(conv.type) && !respondInGroupOnMention) {
                        shouldDispatch = true;
                    }
                    if (!shouldDispatch) continue;

                    // Strip mention trigger
                    std::string cleanText = messageText;
                    if (!mentionTrigger.empty()) {
                        size_t pos;
                        while ((pos = cleanText.find(mentionTrigger)) != std::string::npos)
                            cleanText.erase(pos, mentionTrigger.size());
                        while (!cleanText.empty() && (cleanText[0] == ' ' || cleanText[0] == '\n'))
                            cleanText.erase(0, 1);
                    }

                    std::string prompt = "[Nextcloud Talk message from " + actorName;
                    prompt += " in \"" + conv.displayName + "\"";
                    if (isDm(conv.type)) prompt += " (direct message)";
                    prompt += "]\n" + cleanText;

                    ChannelReplyTarget replyTarget;
                    replyTarget.channel_name = rt->channel_name;
                    replyTarget.channel_type = rt->channel_type;
                    replyTarget.type = ChannelReplyTarget::Chat;
                    replyTarget.peer_id = token;

                    m_ctx.dispatch(rt->agent_id, "conv:" + token, prompt, "nextcloud", replyTarget, "{}");

                    ALOG_DEBUG("nextcloud", "Dispatched from " << actorName
                              << " in " << conv.displayName << " (id=" << msgId << ")");
                }
            }
        } catch (const std::exception& ex) {
            ALOG_ERROR("nextcloud", "Exception: " << ex.what());
            rt->consecutive_errors++;
            int backoff = std::min(60, rt->consecutive_errors * 5);
            std::this_thread::sleep_for(std::chrono::seconds(backoff));
        }
    }

    ALOG_INFO("nextcloud", "Talk poll loop stopped for " << rt->channel_name);
}

void NextcloudAdapter::SendReply(const ChannelReplyTarget& target, const std::string& text) {
    auto* rt = m_runtime.get();
    std::string serverUrl = GetString(rt->config, "server_url");
    std::string username = GetString(rt->config, "username");
    std::string appPassword = GetString(rt->config, "app_password");
    if (serverUrl.empty() || username.empty() || appPassword.empty()) return;

    while (!serverUrl.empty() && serverUrl.back() == '/') serverUrl.pop_back();

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
        ALOG_WARNING("nextcloud", "SendReply failed (" << resp.status_code << ")");
    }
}


// ============================================================================
// MoltbookAdapter — notification poller (#15 card path, day one)
// ============================================================================

void MoltbookAdapter::SendReply(const ChannelReplyTarget&, const std::string&) {
    // delivery=tool: replies flow exclusively through the channels tool
    // (the Lua adapter owns the write path incl. challenge/verification).
    ALOG_WARNING("moltbook", "SendReply called - arrivals are delivery=tool; "
                              "ignoring auto-reply");
}

void MoltbookAdapter::RunLoop() {
    auto* rt = m_runtime.get();

    const std::string apiKey = GetString(rt->config, "api_key");
    if (apiKey.empty()) {
        ALOG_WARNING("moltbook", "No api_key for " << rt->channel_name);
        rt->active = false;
        return;
    }
    const std::string base = GetString(rt->config, "api_base_url",
                                       "https://www.moltbook.com/api/v1");
    int interval = 60;
    try {
        interval = std::stoi(GetString(rt->config, "poll_interval", "60"));
    } catch (...) {}
    if (interval < 15) interval = 15;

    long long lastNotifId = 0;
    if (m_ctx.configStore) {
        const std::string stored = m_ctx.configStore->Get("",
            "channel." + rt->channel_name + ".polling.last_notif_id");
        if (!stored.empty()) {
            try { lastNotifId = std::stoll(stored); } catch (...) {}
        }
    }

    ALOG_INFO("moltbook", "Notification poller started for " << rt->channel_name
              << " (interval " << interval << "s, base " << base << ")");

    while (rt->active && !m_stopRequested) {
        for (int i = 0; i < interval && rt->active && !m_stopRequested; ++i)
            std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!rt->active || m_stopRequested) break;

        HttpClient::Request req;
        req.method = "GET";
        req.url = base + "/notifications?limit=50";
        req.headers["Authorization"] = "Bearer " + apiKey;
        req.headers["Accept"] = "application/json";
        req.follow_redirects = false;
        auto resp = m_ctx.httpClient.Execute(req);
        if (resp.status_code != 200) {
            ALOG_WARNING("moltbook", "notifications poll failed (HTTP "
                        << resp.status_code << ")");
            continue;
        }
        auto data = ParseJson(resp.body);
        if (!data.isObject()) continue;
        const Json::Value notifs = data["notifications"];
        if (!notifs.isArray()) continue;

        long long maxId = lastNotifId;
        for (const auto& n : notifs) {
            long long id = 0;
            bool numeric = false;
            if (n["id"].isNumeric()) {
                id = n["id"].asInt64();
                numeric = true;
            } else if (n["id"].isString()) {
                try { id = std::stoll(n["id"].asString()); numeric = true; }
                catch (...) {}
            }
            if (numeric && id <= lastNotifId) continue;
            if (numeric && id > maxId) maxId = id;
            ProcessNotification(n);
        }
        if (maxId != lastNotifId) {
            lastNotifId = maxId;
            if (m_ctx.configStore)
                m_ctx.configStore->Set("", "channel." + rt->channel_name +
                    ".polling.last_notif_id", std::to_string(lastNotifId));
        }
    }
    ALOG_INFO("moltbook", "Notification poller stopped for " << rt->channel_name);
}

void MoltbookAdapter::ProcessNotification(const Json::Value& n) {
    auto* rt = m_runtime.get();
    const std::string type = GetString(n, "type");
    const std::string from = n["from_agent"].isObject()
        ? GetString(n["from_agent"], "name") : GetString(n, "from");
    const std::string postId = GetString(n, "post_id");
    const std::string commentId = GetString(n, "comment_id");
    const std::string content = GetString(n, "content");

    // Interactive types dispatch as cards; votes/follows are ambient noise.
    if (type != "comment" && type != "reply" && type != "mention") {
        ALOG_DEBUG("moltbook", "skip notification type=" << type);
        return;
    }
    if (content.empty() || postId.empty()) return;

    std::string displayText = "Moltbook " + type + " from " + from +
        " on post " + postId;
    if (!commentId.empty()) displayText += " (in reply to a comment)";
    displayText += ":\n" + content;

    Json::Value meta;
    meta["message_type"] = "wall";
    meta["delivery"] = "tool";
    Json::Value origin;
    origin["user_display"] = from.empty() ? "unknown" : from;
    if (n["from_agent"].isObject() && n["from_agent"].isMember("id"))
        origin["user_id"] = n["from_agent"]["id"].asString();
    meta["origin"] = origin;
    meta["post_id"] = postId;
    meta["thread_root_id"] = postId;
    if (!commentId.empty()) meta["reply_parent_id"] = commentId;
    meta["source_message_id"] = commentId.empty() ? postId : commentId;
    meta["reply_instructions"] =
        "Reply using the channels tool with action=comment or action=reply; "
        "post_id is provided by the arrival and filled automatically. "
        "Include parent_id (also filled) when the reply targets a comment. "
        "Text replies are NOT delivered.";
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    const std::string metadata = Json::writeString(wb, meta);

    ChannelReplyTarget replyTarget;
    replyTarget.channel_name = rt->channel_name;
    replyTarget.channel_type = rt->channel_type;
    replyTarget.type = ChannelReplyTarget::Wall;
    replyTarget.post_id = postId;
    if (!commentId.empty()) replyTarget.reply_to_comment = commentId;

    const std::string routingKey = "wall:moltbook:post:" + postId;
    m_ctx.dispatch(rt->agent_id, routingKey, displayText, "moltbook",
                   replyTarget, metadata);

    ALOG_DEBUG("moltbook", "Dispatched " << type << " notification on post "
              << postId);
}

} // namespace animus::kernel

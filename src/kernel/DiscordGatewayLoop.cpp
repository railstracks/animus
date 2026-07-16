// Discord Gateway WebSocket Loop for Animus ChannelManager
//
// Implements the Discord Gateway v10 protocol:
//   1. GET /gateway/bot → WSS URL + shard info
//   2. Connect to WSS, receive HELLO (opcode 10) with heartbeat_interval
//   3. Send HEARTBEAT (opcode 1) with jitter
//   4. Send IDENTIFY (opcode 2) with token + intents
//   5. Receive READY (opcode 0, t=READY) → cache session_id, resume_gateway_url, user.id
//   6. Receive MESSAGE_CREATE events → dispatch to agent sessions
//   7. Heartbeat every interval; handle HEARTBEAT_ACK (opcode 11)
//   8. On disconnect: RESUME (opcode 6) with session_id + last_seq
//   9. Handle RECONNECT (opcode 7) and INVALID_SESSION (opcode 9)
//
// Architecture mirrors EmailWebSocketLoop: trantor::EventLoop + drogon::WebSocketClient
// ============================================================================

#include "animus_kernel/ChannelManager.h"
#include "animus_kernel/tools/HttpClient.h"

#include <drogon/WebSocketClient.h>
#include <drogon/HttpRequest.h>
#include <trantor/net/EventLoop.h>

#include <json/json.h>
#include <json/writer.h>

#include <chrono>
#include <random>
#include <sstream>
#include <iostream>

namespace animus::kernel {

// ============================================================================
// JSON helpers (local copies — anonymous namespace in ChannelManager.cpp)
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

std::string GetString(const Json::Value& v, const std::string& key,
                       const std::string& def = "") {
    if (v.isMember(key) && v[key].isString()) return v[key].asString();
    return def;
}

} // anonymous namespace

// ============================================================================
// Discord Gateway opcodes
// ============================================================================
namespace discord_op {
    constexpr int DISPATCH      = 0;
    constexpr int HEARTBEAT     = 1;
    constexpr int IDENTIFY      = 2;
    constexpr int STATUS_UPDATE = 3;
    constexpr int RESUME        = 6;
    constexpr int RECONNECT     = 7;
    constexpr int REQUEST_GUILD_MEMBERS = 8;
    constexpr int INVALID_SESSION = 9;
    constexpr int HELLO         = 10;
    constexpr int HEARTBEAT_ACK = 11;
}

// ============================================================================
// Discord Gateway intents bitmask
// ============================================================================
namespace discord_intents {
    constexpr int GUILDS                  = 1 << 0;   //    1
    constexpr int GUILD_MEMBERS           = 1 << 1;   //    2 (privileged)
    constexpr int GUILD_BANS              = 1 << 2;   //    4
    constexpr int GUILD_EMOJIS_STICKERS   = 1 << 3;   //    8
    constexpr int GUILD_INTEGRATIONS      = 1 << 4;   //   16
    constexpr int GUILD_WEBHOOKS          = 1 << 5;   //   32
    constexpr int GUILD_INVITES           = 1 << 6;   //   64
    constexpr int GUILD_VOICE_STATES      = 1 << 7;   //  128
    constexpr int GUILD_PRESENCES         = 1 << 8;   //  256 (privileged)
    constexpr int GUILD_MESSAGES           = 1 << 9;   //  512
    constexpr int GUILD_MESSAGE_REACTIONS  = 1 << 10;  // 1024
    constexpr int GUILD_MESSAGE_TYPING    = 1 << 11;  // 2048
    constexpr int DIRECT_MESSAGES         = 1 << 12;  // 4096
    constexpr int DIRECT_MESSAGE_REACTIONS = 1 << 13;  // 8192
    constexpr int DIRECT_MESSAGE_TYPING   = 1 << 14;  //16384
    constexpr int MESSAGE_CONTENT         = 1 << 15;  //32768 (privileged)

    // Default: GUILDS + GUILD_MESSAGES + GUILD_MESSAGE_REACTIONS + DIRECT_MESSAGES + MESSAGE_CONTENT
    constexpr int DEFAULT = GUILDS | GUILD_MESSAGES | GUILD_MESSAGE_REACTIONS
                          | DIRECT_MESSAGES | MESSAGE_CONTENT;  // 38405
}

void ChannelManager::DiscordGatewayLoop(PollerState* state) {
    std::cerr << "[discord] Gateway loop starting for " << state->channel_name << std::endl;

    std::string botToken = GetString(state->config, "bot_token");
    if (botToken.empty()) {
        std::cerr << "[discord] No bot_token configured for " << state->channel_name
                  << " — cannot start Gateway" << std::endl;
        return;
    }

    // Determine intents (default: 38405)
    int intents = discord_intents::DEFAULT;
    std::string intentsStr = GetString(state->config, "intents");
    if (!intentsStr.empty()) {
        try { intents = std::stoi(intentsStr); } catch (...) {}
    }

    // Step 1: Get Gateway URL
    HttpClient::Request gatewayReq;
    gatewayReq.method = "GET";
    gatewayReq.url = "https://discord.com/api/v10/gateway/bot";
    gatewayReq.headers["Authorization"] = "Bot " + botToken;

    HttpClient::Response gatewayResp = m_httpClient.Execute(gatewayReq);
    if (gatewayResp.status_code != 200) {
        std::cerr << "[discord] Failed to get gateway URL (status "
                  << gatewayResp.status_code << "): " << gatewayResp.body << std::endl;
        return;
    }

    Json::Value gatewayInfo = ParseJson(gatewayResp.body);
    std::string gatewayUrl = GetString(gatewayInfo, "url");
    if (gatewayUrl.empty()) {
        std::cerr << "[discord] No URL in gateway/bot response" << std::endl;
        return;
    }

    // Gateway URL is like wss://gateway.discord.gg
    // Query params go on the HTTP request path, not the WebSocket host string
    std::string wsHostUrl = gatewayUrl;  // wss://gateway.discord.gg
    std::string wsPath = "/?v=10&encoding=json";

    std::cerr << "[discord] Gateway URL: " << wsHostUrl << wsPath << std::endl;

    // Gateway state
    std::string sessionId;
    std::string resumeUrl;
    std::string botUserId;
    uint64_t lastSeq = 0;
    bool identified = false;
    bool heartbeatAcked = true;  // Start true; first heartbeat sent after HELLO
    bool gatewayAlive = true;    // Set false when stop() is called; guards send()
    std::chrono::steady_clock::time_point lastHeartbeatSent;

    // Step 2: Create event loop and WebSocket client
    trantor::EventLoop loop;

    auto wsPtr = drogon::WebSocketClient::newWebSocketClient(wsHostUrl, &loop);

    // --- Message handler: Discord Gateway protocol ---
    wsPtr->setMessageHandler(
        [this, state, &sessionId, &resumeUrl, &botUserId, &lastSeq,
         &identified, &heartbeatAcked, &lastHeartbeatSent, &gatewayAlive, botToken, intents, &wsPtr, &loop]
        (std::string&& message,
         const drogon::WebSocketClientPtr&,
         const drogon::WebSocketMessageType& type) {

        if (type != drogon::WebSocketMessageType::Text) return;

        Json::Value payload = ParseJson(message);
        int op = payload.isMember("op") ? payload["op"].asInt() : -1;
        Json::Value data = payload["d"];
        std::string eventType = GetString(payload, "t");

        // Track sequence number for heartbeat and resume
        if (payload.isMember("s") && !payload["s"].isNull()) {
            lastSeq = payload["s"].asUInt64();
        }

        switch (op) {
        case discord_op::HELLO: {
            // Opcode 10: Hello — start heartbeat and identify
            int heartbeatInterval = data.isMember("heartbeat_interval")
                                    ? data["heartbeat_interval"].asInt() : 41250;
            std::cerr << "[discord] HELLO received, heartbeat_interval="
                      << heartbeatInterval << "ms" << std::endl;

            // First heartbeat with jitter (prevents thundering herd)
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_real_distribution<> dist(0.0, 1.0);
            double jitter = dist(gen);
            int firstBeatMs = static_cast<int>(heartbeatInterval * jitter);

            // Schedule first heartbeat
            lastHeartbeatSent = std::chrono::steady_clock::now();
            loop.runAfter(firstBeatMs / 1000.0, [&loop, heartbeatInterval, &heartbeatAcked,
                                                   &lastHeartbeatSent, &lastSeq, wsPtr, &gatewayAlive]() {
                if (!gatewayAlive) return;
                // Send first heartbeat
                Json::Value hb;
                hb["op"] = discord_op::HEARTBEAT;
                hb["d"] = lastSeq > 0 ? Json::Value(static_cast<Json::Int64>(lastSeq)) : Json::Value();
                Json::StreamWriterBuilder wb;
                wb.settings_["indentation"] = "";
                std::string hbMsg = Json::writeString(wb, hb);
                auto conn = wsPtr->getConnection();
                if (conn) conn->send(hbMsg);
                heartbeatAcked = false;
                lastHeartbeatSent = std::chrono::steady_clock::now();
                std::cerr << "[discord] First heartbeat sent (seq=" << lastSeq << ")" << std::endl;

                // Schedule recurring heartbeat
                loop.runEvery(heartbeatInterval / 1000.0, [&heartbeatAcked, &lastHeartbeatSent,
                                                             &lastSeq, wsPtr, &gatewayAlive]() {
                    if (!gatewayAlive) return;
                    if (!heartbeatAcked) {
                        std::cerr << "[discord] HEARTBEAT_ACK not received — connection dead" << std::endl;
                        gatewayAlive = false;
                        wsPtr->stop();
                        return;
                    }
                    Json::Value hb;
                    hb["op"] = discord_op::HEARTBEAT;
                    hb["d"] = lastSeq > 0 ? Json::Value(static_cast<Json::Int64>(lastSeq)) : Json::Value();
                    Json::StreamWriterBuilder wb2;
                    wb2.settings_["indentation"] = "";
                    std::string msg = Json::writeString(wb2, hb);
                    auto conn = wsPtr->getConnection();
                    if (conn) conn->send(msg);
                    heartbeatAcked = false;
                    lastHeartbeatSent = std::chrono::steady_clock::now();
                });
            });

            // Send IDENTIFY
            Json::Value identify;
            identify["op"] = discord_op::IDENTIFY;
            Json::Value identifyData;
            identifyData["token"] = botToken;
            Json::Value props;
            props["os"] = "linux";
            props["browser"] = "animus";
            props["device"] = "animus";
            identifyData["properties"] = props;
            identifyData["intents"] = intents;
            identify["d"] = identifyData;

            Json::StreamWriterBuilder wb;
            wb.settings_["indentation"] = "";
            std::string identifyMsg = Json::writeString(wb, identify);
            auto conn = wsPtr->getConnection();
            if (conn) conn->send(identifyMsg);
            std::cerr << "[discord] IDENTIFY sent (intents=" << intents << ")" << std::endl;
            break;
        }

        case discord_op::DISPATCH: {
            // Opcode 0: Dispatch event
            if (eventType == "READY") {
                // Cache session info
                sessionId = GetString(data, "session_id");
                resumeUrl = GetString(data, "resume_gateway_url");
                Json::Value user = data["user"];
                botUserId = GetString(user, "id");
                identified = true;
                state->ws_connected = true;
                state->consecutive_errors = 0;

                // Store bot_user_id for self-message filtering
                state->discord_bot_user_id = botUserId;

                std::cerr << "[discord] READY — session_id=" << sessionId
                          << " bot_user_id=" << botUserId << std::endl;
            } else if (eventType == "RESUMED") {
                std::cerr << "[discord] RESUMED — replayed missed events" << std::endl;
            } else if (eventType == "MESSAGE_CREATE") {
                // Self-message filter
                std::string authorId = GetString(data["author"], "id");
                if (!botUserId.empty() && authorId == botUserId) {
                    break;
                }

                std::string content = GetString(data, "content");
                std::string channelId = GetString(data, "channel_id");
                std::string guildId = GetString(data, "guild_id");
                std::string msgId = GetString(data, "id");
                std::string authorUsername = GetString(data["author"], "username");

                // Deduplicate
                if (state->seenEventStrIds.count(msgId)) break;
                state->seenEventStrIds.insert(msgId);
                if (state->seenEventStrIds.size() > 500) {
                    state->seenEventStrIds.erase(state->seenEventStrIds.begin());
                }

                // Check if this is a DM (no guild_id)
                bool isDm = guildId.empty();
                bool isMention = false;

                // Check mentions for bot
                if (data.isMember("mentions") && data["mentions"].isArray()) {
                    for (const auto& mention : data["mentions"]) {
                        if (GetString(mention, "id") == botUserId) {
                            isMention = true;
                            break;
                        }
                    }
                }

                // Decide whether to dispatch
                bool shouldDispatch = false;
                bool respondToDm = true;
                bool respondToMentions = true;

                if (state->config.isMember("respond_to_dm")) {
                    if (state->config["respond_to_dm"].isBool())
                        respondToDm = state->config["respond_to_dm"].asBool();
                }
                if (state->config.isMember("respond_to_mentions")) {
                    if (state->config["respond_to_mentions"].isBool())
                        respondToMentions = state->config["respond_to_mentions"].asBool();
                }

                if (isDm && respondToDm) {
                    // DM whitelist check
                    bool dmWhitelistEnabled = false;
                    if (state->config.isMember("dm_whitelist_enabled") &&
                        state->config["dm_whitelist_enabled"].isString() &&
                        state->config["dm_whitelist_enabled"].asString() == "true") {
                        dmWhitelistEnabled = true;
                    }
                    if (dmWhitelistEnabled &&
                        state->config.isMember("allowed_dm_users") &&
                        state->config["allowed_dm_users"].isArray() &&
                        state->config["allowed_dm_users"].size() > 0) {
                        bool userAllowed = false;
                        for (const auto& u : state->config["allowed_dm_users"]) {
                            if (u.isString() && u.asString() == authorUsername) {
                                userAllowed = true;
                                break;
                            }
                        }
                        if (!userAllowed) break;
                    }
                    shouldDispatch = true;
                } else if (isMention && respondToMentions) {
                    shouldDispatch = true;
                }

                // Also dispatch if channel is in monitored list
                if (!shouldDispatch && !isDm) {
                    if (state->config.isMember("monitored_channels") &&
                        state->config["monitored_channels"].isArray()) {
                        for (const auto& ch : state->config["monitored_channels"]) {
                            if (ch.asString() == channelId) {
                                shouldDispatch = true;
                                break;
                            }
                        }
                    }
                }

                if (!shouldDispatch) break;

                // Build message text
                std::string displayText;
                if (isDm) {
                    displayText = "[DM] " + authorUsername + ": " + content;
                } else {
                    displayText = "[" + channelId + "] " + authorUsername + ": " + content;
                }

                std::cerr << "[discord] MESSAGE_CREATE: " << displayText << std::endl;

                // Update liveness
                state->last_ws_event = std::chrono::steady_clock::now();

                // Dispatch to agent session with proper routing key
                // For Discord, both DMs and guild channels are addressed by channel_id
                // DMs: peer:DM_CHANNEL_ID (so SendReply can POST to /channels/{dm_channel_id}/messages)
                // Guild channels: post:CHANNEL_ID (so SendReply can POST to /channels/{channel_id}/messages)
                std::string routingKey;
                if (isDm) {
                    routingKey = "peer:" + channelId;  // DMs: peer key uses DM channel ID
                } else {
                    routingKey = "post:" + channelId;   // Guild channels: post key uses channel ID
                }
                DispatchToSession(state, routingKey, displayText, "chat");
            } else {
                std::cerr << "[discord] Event: " << eventType << " (seq=" << lastSeq << ")" << std::endl;
            }
            break;
        }

        case discord_op::HEARTBEAT: {
            // Opcode 1: Server requests immediate heartbeat
            Json::Value hb;
            hb["op"] = discord_op::HEARTBEAT;
            hb["d"] = lastSeq > 0 ? Json::Value(static_cast<Json::Int64>(lastSeq)) : Json::Value();
            Json::StreamWriterBuilder wb;
            wb.settings_["indentation"] = "";
            auto conn = wsPtr->getConnection();
            if (conn) conn->send(Json::writeString(wb, hb));
            std::cerr << "[discord] Heartbeat requested by server" << std::endl;
            break;
        }

        case discord_op::RECONNECT: {
            // Opcode 7: Server requests reconnect
            std::cerr << "[discord] RECONNECT requested by server — reconnecting" << std::endl;
            gatewayAlive = false;
            wsPtr->stop();
            break;
        }

        case discord_op::INVALID_SESSION: {
            // Opcode 9: Session invalidated
            bool canResume = data.isBool() ? data.asBool() : false;
            std::cerr << "[discord] INVALID_SESSION — can_resume=" << canResume << std::endl;
            if (!canResume) {
                sessionId.clear();
                resumeUrl.clear();
                identified = false;
            }
            break;
        }

        case discord_op::HEARTBEAT_ACK: {
            // Opcode 11: Heartbeat acknowledged
            heartbeatAcked = true;
            // Update liveness tracker — heartbeats prove the connection is alive
            // even when no MESSAGE_CREATE events arrive (quiet server)
            state->last_ws_event = std::chrono::steady_clock::now();
            break;
        }

        default:
            std::cerr << "[discord] Unknown opcode: " << op << std::endl;
            break;
        }
    });

    // --- Connection closed handler ---
    wsPtr->setConnectionClosedHandler(
        [state](const drogon::WebSocketClientPtr&) {
            std::cerr << "[discord] WebSocket connection closed for "
                      << state->channel_name << std::endl;
            state->ws_connected = false;
        });

    // --- Build connect request ---
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);

    std::cerr << "[discord] Connecting to Gateway: " << wsHostUrl << wsPath << std::endl;

    // --- Connect ---
    wsPtr->connectToServer(
        req,
        [this, state, wsPtr](drogon::ReqResult r,
                             const drogon::HttpResponsePtr&,
                             const drogon::WebSocketClientPtr&) {
            if (r != drogon::ReqResult::Ok) {
                state->ws_connected = false;
                state->consecutive_errors++;
                std::cerr << "[discord] Connection failed (attempt "
                          << state->consecutive_errors << ")" << std::endl;
                if (state->consecutive_errors >= 5) {
                    std::cerr << "[discord] Too many failures — stopping" << std::endl;
                    state->active = false;
                }
                return;
            }

            std::cerr << "[discord] WebSocket connected for " << state->channel_name << std::endl;
            state->consecutive_errors = 0;
        });

    // Initialize liveness tracker
    state->last_ws_event = std::chrono::steady_clock::now();

    // Schedule periodic checks: shutdown + liveness monitoring
    loop.runEvery(5.0, [this, state, &loop, wsPtr, &gatewayAlive]() {
        if (!state->active) {
            std::cerr << "[discord] Shutting down" << std::endl;
            gatewayAlive = false;
            wsPtr->stop();
            loop.quit();
            return;
        }

        // Liveness check: if connected but no events for 5 minutes
        if (state->ws_connected) {
            auto elapsed = std::chrono::steady_clock::now() - state->last_ws_event;
            auto minutes = std::chrono::duration_cast<std::chrono::minutes>(elapsed);
            if (minutes.count() >= 5) {
                std::cerr << "[discord] No events for " << minutes.count()
                          << " minutes (stale connection)" << std::endl;
                state->consecutive_errors++;
                if (state->consecutive_errors >= 3) {
                    std::cerr << "[discord] Stale connection — reconnecting" << std::endl;
                    state->ws_connected = false;
                    gatewayAlive = false;
                    wsPtr->stop();
                    state->consecutive_errors = 0;
                }
            }
        }
    });

    // Run the event loop — blocks until quit() is called from shutdown check
    loop.loop();

    std::cerr << "[discord] Gateway loop stopped for " << state->channel_name << std::endl;
}

} // namespace animus::kernel

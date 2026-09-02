#include "animus_kernel/Log.h"
#include "animus_kernel/ChannelManager.h"

#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <algorithm>
#include <set>
#include <map>
#include <thread>
#include <vector>
#include <iomanip>
#include <ctime>

#include "animus_kernel/AgentConfigStore.h"
#include "animus_kernel/ChannelHelpers.h"
#include "animus_kernel/tools/HttpClient.h"
#include "animus_kernel/whatsapp/WABinary.h"

#include <json/json.h>
#include <json/writer.h>

namespace animus::kernel {

// ============================================================================
// JSON helpers
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

int64_t GetInt(const Json::Value& v, const std::string& key, int64_t def = 0) {
    if (v.isMember(key) && v[key].isInt64()) return v[key].asInt64();
    if (v.isMember(key) && v[key].isInt()) return v[key].asInt();
    return def;
}

std::string GetConfigString(const Json::Value& config, const std::string& key,
                             const std::string& def = "") {
    return GetString(config, key, def);
}

std::string UrlEncode(const std::string& input) {
    // RFC 3986 unreserved characters
    static const std::string unreserved =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    std::string result;
    result.reserve(input.size() * 3);
    for (unsigned char c : input) {
        if (unreserved.find(c) != std::string::npos) {
            result += static_cast<char>(c);
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            result += buf;
        }
    }
    return result;
}

// ISO 8601 UTC timestamp for Bluesky API
std::string iso_now_bsky_cm() {
    auto t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S.000Z");
    return oss.str();
}

// Base64 encoder for HTTP Basic auth
std::string Base64EncodeStr(const std::string& input) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve(((input.size() + 2) / 3) * 4);
    for (size_t i = 0; i < input.size(); i += 3) {
        uint32_t n = static_cast<uint8_t>(input[i]) << 16;
        if (i + 1 < input.size()) n |= static_cast<uint8_t>(input[i + 1]) << 8;
        if (i + 2 < input.size()) n |= static_cast<uint8_t>(input[i + 2]);
        result += table[(n >> 18) & 0x3F];
        result += table[(n >> 12) & 0x3F];
        result += (i + 1 < input.size()) ? table[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < input.size()) ? table[n & 0x3F] : '=';
    }
    return result;
}

} // anonymous namespace

// ============================================================================
// ChannelRouter
// ============================================================================

std::string ChannelRouter::MakeKey(const std::string& channelName,
                                    const std::string& routingKey) {
    return channelName + ":" + routingKey;
}

void ChannelRouter::Register(const std::string& channelName,
                              const std::string& routingKey,
                              const std::string& sessionKey,
                              const std::string& agentId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RoutingEntry entry;
    entry.session_key = sessionKey;
    entry.created = std::chrono::steady_clock::now();
    entry.agent_id = agentId;
    entry.channel_name = channelName;
    m_entries[MakeKey(channelName, routingKey)] = std::move(entry);
}

std::optional<ChannelRouter::RoutingEntry> ChannelRouter::Lookup(
    const std::string& channelName,
    const std::string& routingKey) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(MakeKey(channelName, routingKey));
    if (it != m_entries.end()) return it->second;
    return std::nullopt;
}

void ChannelRouter::PruneExpired(std::chrono::seconds ttl) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto now = std::chrono::steady_clock::now();
    for (auto it = m_entries.begin(); it != m_entries.end(); ) {
        if (now - it->second.created > ttl) {
            it = m_entries.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================================================
// ChannelManager — Construction / Destruction
// ============================================================================

ChannelManager::ChannelManager(HttpClient& httpClient,
                               AgentConfigStore* configStore,
                               DispatchCallback dispatch,
                               LogCallback logCallback,
                               SessionQueryCallback sessionQuery)
    : m_httpClient(httpClient)
    , m_configStore(configStore)
    , m_dispatch(std::move(dispatch))
    , m_logCallback(std::move(logCallback))
    , m_sessionQuery(std::move(sessionQuery)) {
}

ChannelManager::~ChannelManager() {
    Shutdown();
}

// ============================================================================
// Channel CRUD
// ============================================================================

std::vector<ChannelState> ChannelManager::ListChannels() const {
    std::lock_guard<std::mutex> lock(m_channelsMutex);
    std::vector<ChannelState> result;
    result.reserve(m_channels.size());
    for (const auto& [_, state] : m_channels) {
        result.push_back(state);
    }
    return result;
}

std::optional<ChannelState> ChannelManager::GetChannel(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_channelsMutex);
    auto it = m_channels.find(name);
    if (it != m_channels.end()) return it->second;
    return std::nullopt;
}

bool ChannelManager::CreateChannel(const ChannelState& state, std::string* error) {
    if (state.name.empty()) {
        if (error) *error = "Channel name is required";
        return false;
    }
    if (state.type.empty()) {
        if (error) *error = "Channel type is required";
        return false;
    }

    // Validate config
    if (!ValidateConfig(state.type, state.config, error)) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_channelsMutex);
        if (m_channels.count(state.name)) {
            if (error) *error = "Channel already exists: " + state.name;
            return false;
        }
    }

    // Persist to config store
    if (m_configStore) {
        m_configStore->Set("", "channel." + state.name + ".type", state.type);
        m_configStore->Set("", "channel." + state.name + ".enabled",
                           state.enabled ? "true" : "false");
        Json::StreamWriterBuilder wb;
        wb.settings_["indentation"] = "";
        m_configStore->Set("", "channel." + state.name + ".config",
                           Json::writeString(wb, state.config));
    }

    {
        std::lock_guard<std::mutex> lock(m_channelsMutex);
        m_channels[state.name] = state;
    }

    // Auto-start if enabled (async to avoid blocking HTTP handler)
    if (state.enabled && m_running) {
        EnqueueRestart(state.name, state);
    }

    // Sync credential keys from ChannelState.config into AgentConfigStore
    // so Lua adapters can read them via config.get("channels.<platform_id>.<key>")
    SyncChannelCredentialsToConfigStore(state.name, state.type, state.config);

    return true;
}

bool ChannelManager::UpdateChannelConfig(const std::string& name,
                                          const Json::Value& config,
                                          std::string* error) {
    ChannelState state;
    {
        std::lock_guard<std::mutex> lock(m_channelsMutex);
        auto it = m_channels.find(name);
        if (it == m_channels.end()) {
            if (error) *error = "Channel not found: " + name;
            return false;
        }
        state = it->second;
    }

    if (!ValidateConfig(state.type, config, error)) {
        return false;
    }

    // Persist
    if (m_configStore) {
        Json::StreamWriterBuilder wb;
        wb.settings_["indentation"] = "";
        m_configStore->Set("", "channel." + name + ".config",
                           Json::writeString(wb, config));
    }

    {
        std::lock_guard<std::mutex> lock(m_channelsMutex);
        m_channels[name].config = config;
    }

    // Re-sync credential keys into AgentConfigStore BEFORE enqueuing restart
    // so the adapter reads fresh credentials when it starts.
    SyncChannelCredentialsToConfigStore(name, state.type, config);

    // Restart the channel with new config (async to avoid blocking HTTP handler)
    if (m_running && state.enabled) {
        state.config = config;
        EnqueueRestart(name, state);
    }

    return true;
}

bool ChannelManager::DeleteChannel(const std::string& name, std::string* error) {
    {
        std::lock_guard<std::mutex> lock(m_channelsMutex);
        if (!m_channels.count(name)) {
            if (error) *error = "Channel not found: " + name;
            return false;
        }
    }

    StopChannel(name);

    if (m_configStore) {
        m_configStore->DeleteByPrefix("", "channel." + name + ".");
    }

    {
        std::lock_guard<std::mutex> lock(m_channelsMutex);
        m_channels.erase(name);
    }

    return true;
}

bool ChannelManager::SetChannelEnabled(const std::string& name, bool enabled,
                                        std::string* error) {
    ChannelState state;
    {
        std::lock_guard<std::mutex> lock(m_channelsMutex);
        auto it = m_channels.find(name);
        if (it == m_channels.end()) {
            if (error) *error = "Channel not found: " + name;
            return false;
        }
        state = it->second;
        state.enabled = enabled;
        it->second.enabled = enabled;
    }

    if (m_configStore) {
        m_configStore->Set("", "channel." + name + ".enabled",
                           enabled ? "true" : "false");
    }

    if (m_running) {
        // Enqueue async restart — StopChannel is called, then StartChannel
        // only if state.enabled is true. Handles both enable and disable.
        EnqueueRestart(name, state);
    }

    return true;
}

// ============================================================================
// Lifecycle
// ============================================================================

bool ChannelManager::Initialize() {
    // Migrate legacy data first
    MigrateFromLegacy();

    LoadChannelsFromConfigStore();

    // Start all enabled channels
    {
        std::lock_guard<std::mutex> lock(m_channelsMutex);
        for (const auto& [name, state] : m_channels) {
            // Sync credentials for existing channels that were created before
            // the SyncChannelCredentialsToConfigStore feature was added
            SyncChannelCredentialsToConfigStore(name, state.type, state.config);
            if (state.enabled) {
                StartChannel(state);
            }
        }
    }

    m_running = true;
    return true;
}

void ChannelManager::Shutdown() {
    if (!m_running) return;
    m_running = false;
    m_stopRequested = true;

    // Clear pending restarts so the restart thread doesn't race with shutdown
    {
        std::lock_guard<std::mutex> lock(m_restartMutex);
        m_pendingRestarts.clear();
    }

    // Stop all poller threads
    {
        std::lock_guard<std::mutex> lock(m_pollersMutex);
        for (auto& [name, state] : m_pollers) {
            state->active = false;
        }
        for (auto& [name, state] : m_pollers) {
            if (state->thread.joinable()) {
                state->thread.join();
            }
        }
        m_pollers.clear();
    }

    // Stop all adapters
    {
        std::lock_guard<std::mutex> lock(m_adaptersMutex);
        for (auto& [name, adapter] : m_adapters) {
            adapter->Stop();
        }
        m_adapters.clear();
        m_adapterTypes.clear();
    }
}

bool ChannelManager::RestartChannel(const std::string& name, std::string* error) {
    ChannelState state;
    {
        std::lock_guard<std::mutex> lock(m_channelsMutex);
        auto it = m_channels.find(name);
        if (it == m_channels.end()) {
            if (error) *error = "Channel not found: " + name;
            return false;
        }
        state = it->second;
    }
    if (m_running) {
        EnqueueRestart(name, state);
    } else {
        StopChannel(name);
        if (state.enabled) {
            StartChannel(state);
        }
    }
    return true;
}

// ============================================================================
// Runtime status
// ============================================================================

bool ChannelManager::IsChannelConnected(const std::string& name) const {
    // Check adapters first
    {
        std::lock_guard<std::mutex> lock(m_adaptersMutex);
        auto it = m_adapters.find(name);
        if (it != m_adapters.end()) {
            return it->second->IsConnected();
        }
    }
    // Check pollers (Discord, WhatsApp — legacy)
    {
        std::lock_guard<std::mutex> lock(m_pollersMutex);
        auto it = m_pollers.find(name);
        if (it != m_pollers.end()) {
            return it->second->active && it->second->consecutive_errors == 0;
        }
    }
    // Check if channel exists in m_channels (configured but no poller — Bluesky, Twitter)
    {
        std::lock_guard<std::mutex> lock(m_channelsMutex);
        auto it = m_channels.find(name);
        if (it != m_channels.end()) return true;
    }
    return false;
}

// ============================================================================
// SendReply — route auto-reply through the right connector
// ============================================================================

// Get WhatsApp QR URL from poller state (used by admin API)
std::string ChannelManager::GetWhatsAppQrUrl(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_pollersMutex);
    auto it = m_pollers.find(name);
    if (it == m_pollers.end()) return "";
    std::lock_guard<std::mutex> qrLock(it->second->whatsapp_qr_mutex);
    return it->second->whatsapp_qr_url;
}

void ChannelManager::SendReply(const ReplyTarget& target, const std::string& text) {
    // --- Adapter-based dispatch ---
    // For connectors migrated to IChannelAdapter, delegate directly.
    {
        std::lock_guard<std::mutex> lock(m_adaptersMutex);
        auto it = m_adapters.find(target.channel_name);
        if (it != m_adapters.end()) {
            it->second->SendReply(target, text);
            return;
        }
    }

    // --- Legacy dispatch ---
    // For connectors not yet migrated (Discord, WhatsApp, Twitter).
    // IRC is fully adapter-based now; if we reach here for IRC, the adapter wasn't found.

    if (target.channel_type == "irc") {
        ALOG_WARNING("channels", "IRC adapter not found for: "
                  << target.channel_name);
        return;
    }

    if (target.channel_type == "discord") {
        std::string channelId;
        if (!target.peer_id.empty()) channelId = target.peer_id;
        else if (!target.post_id.empty()) channelId = target.post_id;

        std::string botToken;
        {
            std::lock_guard<std::mutex> lock(m_channelsMutex);
            auto it = m_channels.find(target.channel_name);
            if (it != m_channels.end())
                botToken = GetString(it->second.config, "bot_token");
        }
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
        req.body = channel_detail::JsonCompact(body);
        req.follow_redirects = false;

        auto resp = m_httpClient.Execute(req);
        if (resp.status_code != 200 && resp.status_code != 201) {
            ALOG_WARNING("discord", "SendReply failed (" << resp.status_code << ")");
        }
        return;
    }

    if (target.channel_type == "whatsapp") {
        std::lock_guard<std::mutex> lock(m_pollersMutex);
        auto it = m_pollers.find(target.channel_name);
        if (it == m_pollers.end()) return;
        auto* pState = it->second.get();

        if (!pState->config.isMember("_wa_outbox_mutex") || !pState->config.isMember("_wa_outbox"))
            return;

        auto* outboxMutex = reinterpret_cast<std::mutex*>(
            pState->config["_wa_outbox_mutex"].asUInt64());
        auto* outbox = reinterpret_cast<std::vector<OutboundMessage>*>(
            pState->config["_wa_outbox"].asUInt64());

        if (!outboxMutex || !outbox) return;

        {
            // Chat target: peer_id. Group arrivals build Wall targets
            // (post_id = group jid) — accept both so group replies work.
            std::string chatJid = target.peer_id.empty() ? target.post_id : target.peer_id;
            if (chatJid.empty()) {
                ALOG_WARNING("channels", "WhatsApp SendReply: no chat target "
                          << "(peer_id/post_id both empty) — dropping");
                return;
            }
            std::lock_guard<std::mutex> olk(*outboxMutex);
            OutboundMessage msg;
            msg.baseJid = chatJid;
            msg.jid = chatJid;
            msg.text = text;
            msg.is_group = animus::whatsapp::isJidGroup(chatJid);
            outbox->push_back(std::move(msg));
        }
        return;
    }

    if (target.channel_type == "twitter") {
        ALOG_DEBUG("channels", "Twitter auto-reply not yet wired");
        return;
    }

    if (target.channel_type == "bluesky") {
        // Bluesky reply: DM (chat) or post reply (wall)
        std::string pds, accessJwt, did;

        // Try poller state first (has fresh JWTs from polling)
        {
            std::lock_guard<std::mutex> lock(m_pollersMutex);
            auto it = m_pollers.find(target.channel_name);
            if (it != m_pollers.end()) {
                accessJwt = it->second->bsky_access_jwt;
                did = it->second->bsky_did;
                pds = GetString(it->second->config, "pds");
            }
        }

        // Fallback to channel config
        if (accessJwt.empty()) {
            std::lock_guard<std::mutex> lock(m_channelsMutex);
            auto it = m_channels.find(target.channel_name);
            if (it != m_channels.end()) {
                const auto& cfg = it->second.config;
                accessJwt = GetString(cfg, "access_jwt");
                did = GetString(cfg, "did");
                pds = GetString(cfg, "pds");
                std::string handle = GetString(cfg, "handle");
                std::string appPassword = GetString(cfg, "app_password");

                // If no JWT but we have credentials, create a session
                if (accessJwt.empty() && !handle.empty() && !appPassword.empty()) {
                    Json::Value body;
                    body["identifier"] = handle;
                    body["password"] = appPassword;

                    HttpClient::Request authReq;
                    authReq.method = "POST";
                    authReq.url = (pds.empty() ? "https://bsky.social" : pds)
                        + "/xrpc/com.atproto.server.createSession";
                    authReq.headers["Content-Type"] = "application/json";
                    authReq.body = channel_detail::JsonCompact(body);

                    auto authResp = m_httpClient.Execute(authReq);
                    if (authResp.status_code == 200) {
                        auto authData = ParseJson(authResp.body);
                        accessJwt = GetString(authData, "accessJwt");
                        did = GetString(authData, "did");
                    }
                }
            }
        }

        if (pds.empty()) pds = "https://bsky.social";

        // --- DM (chat) path ---
        if (target.type == ReplyTarget::Chat && !target.peer_id.empty()) {
            // peer_id is the convoId
            std::string convoId = target.peer_id;

            Json::Value msgRecord;
            msgRecord["text"] = text;

            Json::Value sendBody;
            sendBody["convoId"] = convoId;
            sendBody["message"] = msgRecord;

            HttpClient::Request chatReq;
            chatReq.method = "POST";
            chatReq.url = "https://api.bsky.chat/xrpc/chat.bsky.convo.sendMessage";
            chatReq.headers["Authorization"] = "Bearer " + accessJwt;
            chatReq.headers["Content-Type"] = "application/json";
            chatReq.body = channel_detail::JsonCompact(sendBody);

            auto chatResp = m_httpClient.Execute(chatReq);
            if (chatResp.status_code != 200) {
                ALOG_WARNING("bluesky", "SendReply: chat sendMessage failed ("
                            << chatResp.status_code << "): " << chatResp.body);
            } else {
                ALOG_DEBUG("bluesky", "SendReply: DM sent to convo " << convoId);
            }
            return;
        }

        // --- Post reply (wall) path ---
        // Build the reply record
        std::string parentUri = target.post_id;
        if (parentUri.empty()) {
            ALOG_WARNING("bluesky", "SendReply: no post_id for reply target");
            return;
        }

        // Resolve parent CID via getRecord
        std::string parentCid;
        {
            // Parse AT-URI: at://<repo>/<collection>/<rkey>
            std::string repo, collection, rkey;
            size_t pos = parentUri.find("//");
            if (pos != std::string::npos) {
                size_t slashPos = parentUri.find('/', pos + 2);
                if (slashPos != std::string::npos) {
                    repo = parentUri.substr(pos + 2, slashPos - pos - 2);
                    size_t nextSlash = parentUri.find('/', slashPos + 1);
                    if (nextSlash != std::string::npos) {
                        collection = parentUri.substr(slashPos + 1, nextSlash - slashPos - 1);
                        rkey = parentUri.substr(nextSlash + 1);
                    }
                }
            }
            if (!repo.empty() && !collection.empty() && !rkey.empty()) {
                std::string recordUrl = pds + "/xrpc/com.atproto.repo.getRecord?repo="
                    + UrlEncode(repo) + "&collection=" + UrlEncode(collection)
                    + "&rkey=" + UrlEncode(rkey);
                HttpClient::Request recReq;
                recReq.method = "GET";
                recReq.url = recordUrl;
                recReq.headers["Authorization"] = "Bearer " + accessJwt;
                auto recResp = m_httpClient.Execute(recReq);
                if (recResp.status_code == 200) {
                    auto recData = ParseJson(recResp.body);
                    parentCid = GetString(recData, "cid");
                }
            }
        }
        if (parentCid.empty()) {
            ALOG_WARNING("bluesky", "SendReply: could not resolve parent CID for " << parentUri);
            return;
        }

        // Build reply record
        Json::Value record;
        record["$type"] = "app.bsky.feed.post";
        record["text"] = text;
        record["createdAt"] = iso_now_bsky_cm();
        record["langs"][0] = "en";
        Json::Value replyObj;
        replyObj["root"]["uri"] = parentUri;
        replyObj["root"]["cid"] = parentCid;
        replyObj["parent"]["uri"] = parentUri;
        replyObj["parent"]["cid"] = parentCid;
        record["reply"] = replyObj;

        Json::Value createBody;
        createBody["repo"] = did;
        createBody["collection"] = "app.bsky.feed.post";
        createBody["record"] = record;

        HttpClient::Request postReq;
        postReq.method = "POST";
        postReq.url = pds + "/xrpc/com.atproto.repo.createRecord";
        postReq.headers["Authorization"] = "Bearer " + accessJwt;
        postReq.headers["Content-Type"] = "application/json";
        postReq.body = channel_detail::JsonCompact(createBody);

        auto postResp = m_httpClient.Execute(postReq);
        if (postResp.status_code != 200) {
            ALOG_WARNING("bluesky", "SendReply: createRecord failed (" << postResp.status_code << ")");
        } else {
            ALOG_DEBUG("bluesky", "SendReply: posted reply to " << parentUri);
        }
        return;
    }

    ALOG_DEBUG("channels", "SendReply: unsupported type: "
              << target.channel_type);
}


// ============================================================================
// Async restart queue — processes channel restarts off the HTTP handler thread
// ============================================================================

void ChannelManager::EnqueueRestart(const std::string& name,
                                     const ChannelState& state) {
    {
        std::lock_guard<std::mutex> lock(m_restartMutex);
        // Coalesce: remove any existing pending restart for this channel
        m_pendingRestarts.erase(
            std::remove_if(m_pendingRestarts.begin(), m_pendingRestarts.end(),
                [&](const PendingRestart& r) { return r.channel_name == name; }),
            m_pendingRestarts.end());
        m_pendingRestarts.push_back({name, state});
    }
    // Spawn worker thread if not already running
    if (!m_restartThreadRunning.exchange(true)) {
        std::thread(&ChannelManager::ProcessPendingRestarts, this).detach();
    }
}

void ChannelManager::ProcessPendingRestarts() {
    while (true) {
        PendingRestart restart;
        {
            std::lock_guard<std::mutex> lock(m_restartMutex);
            if (m_pendingRestarts.empty()) {
                m_restartThreadRunning = false;
                break;
            }
            restart = std::move(m_pendingRestarts.front());
            m_pendingRestarts.erase(m_pendingRestarts.begin());
        }

        // Stop the old channel instance
        StopChannel(restart.channel_name);

        // Brief pause to allow socket/file cleanup to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Start with new config (only if still enabled)
        if (restart.state.enabled) {
            StartChannel(restart.state);
        }
    }
}

// ============================================================================
// StartChannel / StopChannel — dispatch to the right connector type
// ============================================================================

void ChannelManager::StartChannel(const ChannelState& state) {
    // --- Adapter-based connectors ---
    // These use IChannelAdapter implementations with their own loop threads.
    // Discord and WhatsApp still use the legacy poller path (deep coupling).
    if (state.type == "irc" || state.type == "telegram" || state.type == "vk" ||
        state.type == "email" || state.type == "slack" ||
        state.type == "nextcloud" || state.type == "moltbook") {

        // Ensure channel context is initialized
        if (!m_channelCtx) {
            m_channelCtx = std::make_unique<ChannelContext>(
                ChannelContext{
                    m_httpClient, m_configStore, m_router,
                    m_dispatch, m_logCallback
                });
        }

        std::unique_ptr<IChannelAdapter> adapter;
        std::string err;

        if (state.type == "irc") {
            adapter = std::make_unique<IrcAdapter>(*m_channelCtx);
        } else if (state.type == "telegram") {
            adapter = std::make_unique<TelegramAdapter>(*m_channelCtx);
        } else if (state.type == "vk") {
            adapter = std::make_unique<VkAdapter>(*m_channelCtx);
        } else if (state.type == "email") {
            adapter = std::make_unique<EmailAdapter>(*m_channelCtx);
        } else if (state.type == "slack") {
            adapter = std::make_unique<SlackAdapter>(*m_channelCtx);
        } else if (state.type == "nextcloud") {
            adapter = std::make_unique<NextcloudAdapter>(*m_channelCtx);
        } else if (state.type == "moltbook") {
            adapter = std::make_unique<MoltbookAdapter>(*m_channelCtx);
        }

        if (adapter && adapter->Start(state, &err)) {
            std::lock_guard<std::mutex> lock(m_adaptersMutex);
            m_adapters[state.name] = std::move(adapter);
            m_adapterTypes[state.name] = state.type;
            ALOG_INFO("channels", "Started " << state.type
                      << " adapter: " << state.name);
        } else {
            ALOG_WARNING("channels", "Failed to start " << state.type
                      << " adapter: " << state.name
                      << " — " << err);
        }
        return;
    }

    // --- Legacy connectors (Discord, WhatsApp) ---
    // These still use PollerState + ChannelManager loop methods.
    // They will be migrated to adapters in a future refactor.
    if (state.type == "discord") {
        auto poller = std::make_unique<PollerState>();
        poller->channel_name = state.name;
        poller->channel_type = state.type;
        poller->config = state.config;
        poller->agent_id = GetString(state.config, "agent_id", "");
        poller->next_attempt = std::chrono::steady_clock::now();
        poller->active = true;

        std::string name = state.name;
        poller->thread = std::thread(&ChannelManager::DiscordGatewayLoop, this, poller.get());

        {
            std::lock_guard<std::mutex> lock(m_pollersMutex);
            m_pollers[name] = std::move(poller);
        }

        ALOG_INFO("channels", "Started Discord Gateway (legacy): " << state.name);
        return;
    }

    if (state.type == "whatsapp") {
        auto poller = std::make_unique<PollerState>();
        poller->channel_name = state.name;
        poller->channel_type = state.type;
        poller->config = state.config;
        poller->agent_id = GetString(state.config, "agent_id", "");
        poller->next_attempt = std::chrono::steady_clock::now();
        poller->active = true;

        std::string name = state.name;
        poller->thread = std::thread(&ChannelManager::WhatsAppGatewayLoop, this, poller.get());

        {
            std::lock_guard<std::mutex> lock(m_pollersMutex);
            m_pollers[name] = std::move(poller);
        }

        ALOG_INFO("channels", "Started WhatsApp Gateway (legacy): " << state.name);
        return;
    }

    // --- Bluesky (REST polling via AT Protocol) ---
    if (state.type == "bluesky") {
        auto poller = std::make_unique<PollerState>();
        poller->channel_name = state.name;
        poller->channel_type = state.type;
        poller->config = state.config;
        poller->agent_id = GetString(state.config, "agent_id", "");
        poller->next_attempt = std::chrono::steady_clock::now();
        poller->active = true;

        std::string name = state.name;
        poller->thread = std::thread(&ChannelManager::BlueskyPollLoop, this, poller.get());

        {
            std::lock_guard<std::mutex> lock(m_pollersMutex);
            m_pollers[name] = std::move(poller);
        }

        ALOG_INFO("channels", "Started Bluesky poller: " << state.name);
        return;
    }

    ALOG_DEBUG("channels", "Unknown channel type: " << state.type);
}

void ChannelManager::StopChannel(const std::string& name) {
    // Stop adapter-based connector
    {
        std::lock_guard<std::mutex> lock(m_adaptersMutex);
        auto it = m_adapters.find(name);
        if (it != m_adapters.end()) {
            it->second->Stop();
            m_adapters.erase(it);
            m_adapterTypes.erase(name);
            ALOG_INFO("channels", "Stopped adapter: " << name);
            return;
        }
    }

    // Stop poller (legacy — Discord/WhatsApp)
    {
        std::lock_guard<std::mutex> lock(m_pollersMutex);
        auto it = m_pollers.find(name);
        if (it != m_pollers.end()) {
            it->second->active = false;
            if (it->second->thread.joinable()) {
                it->second->thread.join();
            }
            m_pollers.erase(it);
        }
    }
}

// ============================================================================
// Config store helpers
// ============================================================================

void ChannelManager::LoadChannelsFromConfigStore() {
    if (!m_configStore) return;

    auto keys = m_configStore->ListKeys("");
    std::set<std::string> channelNames;

    for (const auto& key : keys) {
        if (key.substr(0, 8) != "channel.") continue;
        std::string rest = key.substr(8);
        auto dotPos = rest.find('.');
        if (dotPos == std::string::npos) continue;
        channelNames.insert(rest.substr(0, dotPos));
    }

    for (const auto& name : channelNames) {
        std::string prefix = "channel." + name + ".";
        std::string type = m_configStore->Get("", prefix + "type");
        if (type.empty()) continue;

        std::string enabledStr = m_configStore->Get("", prefix + "enabled");
        bool enabled = (enabledStr == "true" || enabledStr == "1");

        std::string configStr = m_configStore->Get("", prefix + "config");
        Json::Value config = ParseJson(configStr);
        if (config.isNull()) config = Json::objectValue;

        ChannelState state;
        state.name = name;
        state.type = type;
        state.enabled = enabled;
        state.config = config;

        ALOG_DEBUG("channels", "Found channel: " << name << " type=" << type
                  << " enabled=" << enabledStr);
        m_channels[name] = state;
    }

    ALOG_INFO("channels", "Loaded " << m_channels.size() << " channels from config store");
}

std::string ChannelManager::GetConfigString(const std::string& name,
                                              const std::string& key,
                                              const std::string& defaultVal) const {
    // Read from in-memory config
    std::lock_guard<std::mutex> lock(m_channelsMutex);
    auto it = m_channels.find(name);
    if (it != m_channels.end()) {
        return GetString(it->second.config, key, defaultVal);
    }
    return defaultVal;
}

// ============================================================================
// Validation
// ============================================================================

void ChannelManager::SyncChannelCredentialsToConfigStore(
        const std::string& name,
        const std::string& type,
        const Json::Value& config) {
    if (!m_configStore) return;

    // Build the platform_id: "type:name" (e.g. "moltbook:moltbook", "bluesky:personal")
    std::string platformId = type + ":" + name;
    std::string prefix = "channels." + platformId + ".";

    // Use the agent_id from the channel config as the config store namespace.
    // Lua adapters read via config.get() which passes their agentId — the key
    // must be stored under the same agent_id to be found.
    std::string agentId = config.get("agent_id", "").asString();

    std::vector<std::string> agentIds;
    if (!agentId.empty()) {
        agentIds.push_back(agentId);
    }

    // Keys that should be synced from ChannelState.config to AgentConfigStore
    // These are the credential fields that Lua adapters read via config.get()
    static const std::vector<std::string> credentialKeys = {
        // Common
        "api_key", "access_token", "bot_token", "app_token", "app_password",
        "client_id", "client_secret", "refresh_token",
        // Base-URL overrides read by Lua adapters (moltbook mock harness;
        // any adapter whose api_base can be overridden needs it mirrored)
        "api_base_url",
        // Bluesky
        "handle", "pds", "access_jwt", "refresh_jwt", "did",
        // VK
        "group_id",
        // Twitter
        "tier",
        // Discord
        "application_id",
        // IRC
        "nick", "host",
        // Moltbook
        "agent_name",
        // Email
        "inbox_id", "backend",
        // Nextcloud Talk
        "server_url", "username", "app_password",
    };

    for (const auto& key : credentialKeys) {
        if (config.isMember(key)) {
            std::string value;
            if (config[key].isString()) {
                value = config[key].asString();
            } else if (config[key].isInt()) {
                value = std::to_string(config[key].asInt());
            } else if (config[key].isBool()) {
                value = config[key].asBool() ? "true" : "false";
            }
            if (!value.empty()) {
                for (const auto& aid : agentIds) {
                    m_configStore->Set(aid, prefix + key, value);
                }
            }
        }
    }
}

bool ChannelManager::ValidateConfig(const std::string& type,
                                     const Json::Value& config,
                                     std::string* error) {
    if (type == "irc") {
        if (GetString(config, "host").empty()) {
            if (error) *error = "IRC host is required";
            return false;
        }
        if (GetString(config, "nick").empty()) {
            if (error) *error = "IRC nick is required";
            return false;
        }
    } else if (type == "telegram") {
        // Token may be empty on edit (kept secret) — only required on create
        // Validation is best-effort here
    } else if (type == "discord") {
        if (!config.isMember("bot_token") || !config["bot_token"].isString()
            || config["bot_token"].asString().empty()) {
            if (error) *error = "Discord channel requires a bot_token";
            return false;
        }
    } else if (type == "vk") {
        // Same — credentials validated at runtime
    } else if (type == "whatsapp") {
        // WhatsApp uses Linked Devices protocol — no token needed
        // auth_dir defaults to /tmp/wa-auth if not specified
    } else if (type == "email") {
        if (GetString(config, "api_key").empty()) {
            if (error) *error = "AgentMail API key is required";
            return false;
        }
        if (GetString(config, "inbox_id").empty()) {
            if (error) *error = "AgentMail inbox ID is required";
            return false;
        }
    } else if (type == "nextcloud") {
        if (GetString(config, "server_url").empty()) {
            if (error) *error = "Nextcloud server URL is required";
            return false;
        }
        if (GetString(config, "username").empty()) {
            if (error) *error = "Nextcloud username is required";
            return false;
        }
        if (GetString(config, "app_password").empty()) {
            if (error) *error = "Nextcloud app password is required";
            return false;
        }
    }
    return true;
}

// ============================================================================
// Migration from legacy systems
// ============================================================================

int ChannelManager::MigrateFromLegacy() {
    int migrated = 0;
    if (!m_configStore) return 0;

    // --- Migrate social.* keys ---
    auto keys = m_configStore->ListKeys("");
    std::map<std::string, std::map<std::string, std::string>> socialInstances;

    for (const auto& key : keys) {
        if (key.substr(0, 7) != "social.") continue;
        std::string rest = key.substr(7);
        auto dotPos = rest.find('.');
        if (dotPos == std::string::npos) continue;

        std::string platformId = rest.substr(0, dotPos);
        std::string field = rest.substr(dotPos + 1);
        std::string value = m_configStore->Get("", key);
        if (!value.empty()) {
            socialInstances[platformId][field] = value;
        }
    }

    for (const auto& [platformId, fields] : socialInstances) {
        // Skip if already migrated
        if (m_configStore->Get("", "channel." + platformId + ".type") != "") continue;

        auto typeIt = fields.find("type");
        if (typeIt == fields.end()) continue;

        std::string type = typeIt->second;

        // Build config blob from flat fields
        Json::Value config(Json::objectValue);
        for (const auto& [field, value] : fields) {
            if (field == "type" || field == "polling.enabled" ||
                field == "polling.method" || field == "polling.wait" ||
                field == "polling.interval" || field == "polling.events" ||
                field == "polling.session_ttl" || field == "polling.ts" ||
                field == "polling.key" || field == "polling.server" ||
                field == "polling.last_update_id") {
                continue; // Skip meta fields
            }
            config[field] = value;
        }

        // Write as channel
        m_configStore->Set("", "channel." + platformId + ".type", type);
        m_configStore->Set("", "channel." + platformId + ".enabled", "true");
        Json::StreamWriterBuilder wb;
        wb.settings_["indentation"] = "";
        m_configStore->Set("", "channel." + platformId + ".config",
                           Json::writeString(wb, config));

        // Delete old keys
        m_configStore->DeleteByPrefix("", "social." + platformId + ".");

        ALOG_DEBUG("channels", "Migrated social instance: " << platformId);
        migrated++;
    }

    // --- Migrate interfaces.json ---
    // TODO: Read interfaces.json file, write to config store, rename file

    if (migrated > 0) {
        ALOG_DEBUG("channels", "Migrated " << migrated << " legacy social instances");
    }
    return migrated;
}

// ============================================================================
// Session dispatch — unified path for all connectors
// ============================================================================

void ChannelManager::LogToSession(PollerState* state,
                                  const std::string& routingKey,
                                  const std::string& message,
                                  const std::string& sessionType) {
    bool isPeer = (routingKey.size() > 5 && routingKey.substr(0, 5) == "peer:");
    bool isPost = (routingKey.size() > 5 && routingKey.substr(0, 5) == "post:");

    std::string routingValue = routingKey.substr(5);

    // Look up existing session
    auto entry = m_router.Lookup(state->channel_name, routingKey);

    std::string sessionKey;
    if (entry) {
        sessionKey = entry->session_key;
    } else {
        sessionKey = sessionType + ":" + state->channel_name + ":" + routingValue;
        m_router.Register(state->channel_name, routingKey, sessionKey, state->agent_id);
    }

    // Log the message without triggering a chain
    m_logCallback(state->agent_id, sessionKey, message, sessionType);
}

// ============================================================================
// Bluesky Poll Loop
// ============================================================================

bool ChannelManager::BlueskyReAuth(PollerState* state) {
    std::string handle = GetString(state->config, "handle");
    std::string appPassword = GetString(state->config, "app_password");
    std::string pds = GetString(state->config, "pds");
    if (pds.empty()) pds = "https://bsky.social";

    if (handle.empty() || appPassword.empty()) {
        ALOG_ERROR("bluesky", "missing handle or app_password for " << state->channel_name);
        return false;
    }

    Json::Value body;
    body["identifier"] = handle;
    body["password"] = appPassword;

    HttpClient::Request req;
    req.method = "POST";
    req.url = pds + "/xrpc/com.atproto.server.createSession";
    req.headers["Content-Type"] = "application/json";
    req.body = channel_detail::JsonCompact(body);

    auto resp = m_httpClient.Execute(req);
    if (resp.status_code != 200) {
        ALOG_ERROR("bluesky", "createSession failed (" << resp.status_code
                  << "): " << resp.body.substr(0, 200));
        return false;
    }

    auto data = ParseJson(resp.body);
    state->bsky_access_jwt = GetString(data, "accessJwt");
    state->bsky_refresh_jwt = GetString(data, "refreshJwt");
    state->bsky_did = GetString(data, "did");

    state->bsky_next_refresh = std::chrono::steady_clock::now() + std::chrono::minutes(90);
    ALOG_INFO("bluesky", "authenticated as " << state->bsky_did
              << " (" << GetString(data, "handle") << ")");
    return true;
}

std::string ChannelManager::BlueskyResolveParentCid(PollerState* state, const std::string& uri) {
    // Parse AT-URI: at://<repo>/<collection>/<rkey>
    size_t pos = uri.find("//");
    if (pos == std::string::npos) return "";
    size_t slashPos = uri.find('/', pos + 2);
    if (slashPos == std::string::npos) return "";
    std::string repo = uri.substr(pos + 2, slashPos - pos - 2);

    size_t nextSlash = uri.find('/', slashPos + 1);
    if (nextSlash == std::string::npos) return "";
    std::string collection = uri.substr(slashPos + 1, nextSlash - slashPos - 1);
    std::string rkey = uri.substr(nextSlash + 1);

    std::string pds = GetString(state->config, "pds");
    if (pds.empty()) pds = "https://bsky.social";

    std::string url = pds + "/xrpc/com.atproto.repo.getRecord?repo="
        + UrlEncode(repo) + "&collection=" + UrlEncode(collection)
        + "&rkey=" + UrlEncode(rkey);

    HttpClient::Request req;
    req.method = "GET";
    req.url = url;
    req.headers["Authorization"] = "Bearer " + state->bsky_access_jwt;

    auto resp = m_httpClient.Execute(req);
    if (resp.status_code != 200) return "";

    auto data = ParseJson(resp.body);
    return GetString(data, "cid");
}

void ChannelManager::BlueskyPollLoop(PollerState* state) {
    ALOG_INFO("bluesky", "poll loop starting for " << state->channel_name);

    // Restore persisted notification watermark (survives daemon restarts).
    // Without this, every restart reprocesses the first notifications page —
    // the agent re-replies to old mentions/quotes (observed: 12 identical
    // replies across one day of container restarts).
    if (state->bsky_last_seen.empty() && m_configStore) {
        state->bsky_last_seen = m_configStore->Get("",
            "social." + state->channel_name + ".last_seen");
        if (!state->bsky_last_seen.empty()) {
            ALOG_INFO("bluesky", "restored notification watermark "
                      << state->bsky_last_seen << " for " << state->channel_name);
        }
    }

    // Initial auth
    if (!BlueskyReAuth(state)) {
        ALOG_ERROR("bluesky", "initial auth failed for " << state->channel_name);
        state->active = false;
        return;
    }

    while (state->active && !m_stopRequested) {
        auto now = std::chrono::steady_clock::now();

        // Sleep until next poll
        if (now < state->next_attempt) {
            auto sleepMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                state->next_attempt - now).count();
            std::this_thread::sleep_for(std::chrono::milliseconds(
                std::min(sleepMs, (int64_t)30000)));
            continue;
        }

        // Refresh auth if needed
        if (now >= state->bsky_next_refresh || state->bsky_access_jwt.empty()) {
            // Try refresh token first
            bool refreshed = false;
            if (!state->bsky_refresh_jwt.empty()) {
                std::string pds = GetString(state->config, "pds");
                if (pds.empty()) pds = "https://bsky.social";

                HttpClient::Request refreshReq;
                refreshReq.method = "POST";
                refreshReq.url = pds + "/xrpc/com.atproto.server.refreshSession";
                refreshReq.headers["Authorization"] = "Bearer " + state->bsky_refresh_jwt;
                refreshReq.headers["Content-Type"] = "application/json";
                refreshReq.body = "";

                auto refreshResp = m_httpClient.Execute(refreshReq);
                if (refreshResp.status_code == 200) {
                    auto refreshData = ParseJson(refreshResp.body);
                    state->bsky_access_jwt = GetString(refreshData, "accessJwt");
                    state->bsky_refresh_jwt = GetString(refreshData, "refreshJwt");
                    if (!GetString(refreshData, "did").empty())
                        state->bsky_did = GetString(refreshData, "did");
                    state->bsky_next_refresh = now + std::chrono::minutes(90);
                    ALOG_INFO("bluesky", "token refreshed for " << state->channel_name);
                    refreshed = true;
                }
            }
            if (!refreshed) {
                ALOG_WARNING("bluesky", "refresh failed, full re-auth for " << state->channel_name);
                if (!BlueskyReAuth(state)) {
                    state->consecutive_errors++;
                    int backoff = std::min(300, 30 * state->consecutive_errors);
                    state->next_attempt = now + std::chrono::seconds(backoff);
                    continue;
                }
            }
        }

        std::string pds = GetString(state->config, "pds");
        if (pds.empty()) pds = "https://bsky.social";

        // --- Notification (posts) polling ---
        bool enablePosts = GetString(state->config, "enable_posts") != "false";
        if (enablePosts) {
            HttpClient::Request req;
            req.method = "GET";
            req.url = pds + "/xrpc/app.bsky.notification.listNotifications?limit=50";
            req.headers["Authorization"] = "Bearer " + state->bsky_access_jwt;

            auto resp = m_httpClient.Execute(req);

            if (resp.status_code == 401 || resp.status_code == 400) {
                ALOG_WARNING("bluesky", "got " << resp.status_code << " — forcing re-auth");
                state->bsky_next_refresh = std::chrono::steady_clock::time_point::min();
                state->next_attempt = now + std::chrono::seconds(5);
                continue;
            }

            if (resp.status_code != 200) {
                ALOG_WARNING("bluesky", "listNotifications failed (" << resp.status_code << ")");
                state->consecutive_errors++;
                int backoff = std::min(300, 30 * state->consecutive_errors);
                state->next_attempt = now + std::chrono::seconds(backoff);
                continue;
            }

            state->consecutive_errors = 0;
            auto data = ParseJson(resp.body);

            if (data.isNull() || !data.isMember("notifications")) {
                state->next_attempt = now + std::chrono::seconds(60);
                continue;
            }

            const auto& notifs = data["notifications"];
            std::string latestSeen = state->bsky_last_seen;
            int processed = 0;
            int totalNotifs = notifs.size();

            ALOG_INFO("bluesky", "listNotifications returned " << totalNotifs
                      << " notifications, watermark=" << state->bsky_last_seen);

            for (const auto& n : notifs) {
                std::string reason = GetString(n, "reason");
                std::string indexedAt = GetString(n, "indexedAt");

                if (!state->bsky_last_seen.empty() && indexedAt <= state->bsky_last_seen)
                    continue;

                ALOG_DEBUG("bluesky", "notification reason=" << reason << " indexedAt=" << indexedAt);

                if (reason != "mention" && reason != "reply" && reason != "quote")
                    continue;

                std::string authorHandle, authorDisplayName, authorDid;
                if (n.isMember("author")) {
                    authorHandle = GetString(n["author"], "handle");
                    authorDisplayName = GetString(n["author"], "displayName");
                    authorDid = GetString(n["author"], "did");
                    if (authorDisplayName.empty()) authorDisplayName = authorHandle;
                }

                std::string postText;
                std::string postUri;
                if (n.isMember("record"))
                    postText = GetString(n["record"], "text");
                postUri = GetString(n, "uri");

                if (postText.empty()) continue;

                // --- Session-metadata dedup (survives watermark loss) ---
                // The post URI is stored as metadata on its session after the
                // first dispatch; re-fires (restart with lost watermark, cursor
                // reset, notification re-index) are skipped. Mirrors the DM
                // rev-ID dedup in BlueskyChatPollLoop.
                if (m_sessionQuery && !postUri.empty()) {
                    // Session keys are thread-scoped as of Aug 31: probe the
                    // wall-typed, root-keyed session this post will route to.
                    std::string probeRoot = postUri;
                    if (n.isMember("record") && n["record"].isMember("reply")
                        && n["record"]["reply"].isMember("root")) {
                        std::string r0 = GetString(n["record"]["reply"]["root"], "uri");
                        if (!r0.empty()) probeRoot = r0;
                    }
                    std::string postSessionKey = "wall:" + state->channel_name
                        + ":" + probeRoot;
                    auto stored = m_sessionQuery(postSessionKey, "bluesky_post_uris");
                    bool alreadyProcessed = false;
                    for (const auto& u : stored) {
                        if (u == postUri) { alreadyProcessed = true; break; }
                    }
                    if (alreadyProcessed) {
                        ALOG_INFO("bluesky", "skipping already-processed "
                                  << reason << " " << postUri);
                        if (latestSeen.empty() || indexedAt > latestSeen)
                            latestSeen = indexedAt;
                        continue;
                    }
                }

                // --- Auto-reply filtering ---
                bool autoReply = GetString(state->config, "auto_reply") != "false";
                bool replyToAll = GetString(state->config, "reply_to_all") != "false";

                if (!autoReply) {
                    ALOG_DEBUG("bluesky", "auto_reply disabled, skipping " << reason
                              << " from @" << authorHandle << " for " << state->channel_name);
                    if (latestSeen.empty() || indexedAt > latestSeen)
                        latestSeen = indexedAt;
                    continue;
                }

                if (!replyToAll) {
                    bool inAllowlist = false;
                    if (state->config.isMember("reply_to_users") && state->config["reply_to_users"].isArray()) {
                        for (const auto& u : state->config["reply_to_users"]) {
                            if (u.asString() == authorHandle) {
                                inAllowlist = true;
                                break;
                            }
                        }
                    }
                    if (!inAllowlist) {
                        ALOG_DEBUG("bluesky", "@" << authorHandle << " not in reply_to_users allowlist, skipping");
                        if (latestSeen.empty() || indexedAt > latestSeen)
                            latestSeen = indexedAt;
                        continue;
                    }
                }

                // Thread refs: for replies, record.reply.root.uri is the thread
                // root (may differ from the parent when replying mid-thread).
                // For mentions/quotes the post itself is the root.
                std::string rootUri = postUri;
                if (n.isMember("record") && n["record"].isMember("reply")
                    && n["record"]["reply"].isMember("root")) {
                    std::string r = GetString(n["record"]["reply"]["root"], "uri");
                    if (!r.empty()) rootUri = r;
                }

                // Parent / root context for replies — mini-hydration so the
                // session opens with the conversation it is answering (#47-
                // flavored: platform as source of truth). Non-fatal on failure.
                std::string parentUri, parentAuthor, parentText, rootAuthor, rootText;
                if (reason == "reply" && n.isMember("record")
                    && n["record"].isMember("reply")) {
                    parentUri = GetString(n["record"]["reply"]["parent"], "uri");
                    std::string pds = GetString(state->config, "pds");
                    if (pds.empty()) pds = "https://bsky.social";
                    auto fetchPost = [&](const std::string& uri,
                                         std::string& author, std::string& text) {
                        if (uri.empty() || state->bsky_access_jwt.empty()) return;
                        HttpClient::Request pr;
                        pr.method = "GET";
                        pr.url = pds + "/xrpc/app.bsky.feed.getPostThread?uri="
                                 + UrlEncode(uri) + "&depth=0";
                        pr.headers["Authorization"] = "Bearer " + state->bsky_access_jwt;
                        auto pres = m_httpClient.Execute(pr);
                        if (pres.status_code != 200) return;
                        auto pd = ParseJson(pres.body);
                        if (!pd.isMember("thread") || !pd["thread"].isMember("post")) return;
                        const auto& ppost = pd["thread"]["post"];
                        if (ppost.isMember("record")) text = GetString(ppost["record"], "text");
                        if (ppost.isMember("author")) author = GetString(ppost["author"], "handle");
                    };
                    fetchPost(parentUri, parentAuthor, parentText);
                    if (rootUri != parentUri)
                        fetchPost(rootUri, rootAuthor, rootText);
                }

                // Body: attribution header only — ids and delivery semantics
                // live in the context card (#15), never inline in the body.
                std::string message;
                if (reason == "reply") {
                    message = "Bluesky post from " + authorDisplayName + " (@" + authorHandle
                            + ") in thread:\n" + postText;
                    if (!parentText.empty())
                        message += "\n\n--- In reply to ---\n@" + parentAuthor
                                 + ": " + parentText;
                    if (!rootText.empty())
                        message += "\n\n--- Thread root ---\n@" + rootAuthor
                                 + ": " + rootText;
                } else {
                    // mention / quote: top-level post addressing us
                    message = "Bluesky post from " + authorDisplayName + " (@" + authorHandle
                            + "):\n" + postText;
                }

                ALOG_INFO("bluesky", "dispatching " << reason << " from @"
                          << authorHandle << " for " << state->channel_name);

                // Dispatch metadata → context card fields (#15/#42):
                // origin map (user = handle, user_id = DID), typed reply-target
                // fields (post_id = the post to answer, thread_root_id = root),
                // explicit reply_instructions. Delivery is tool-only (wall).
                Json::Value meta;
                meta["message_type"] = "wall";
                Json::Value origin;
                origin["user"] = "@" + authorHandle;
                if (authorDisplayName != authorHandle)
                    origin["user_display"] = authorDisplayName;
                if (!authorDid.empty())
                    origin["user_id"] = authorDid; // did:plc — stable id (#42)
                if (reason == "reply")
                    origin["channel"] = "thread";
                meta["origin"] = origin;
                meta["post_id"] = postUri;
                meta["thread_root_id"] = rootUri;
                if (!parentUri.empty())
                    meta["reply_parent_id"] = parentUri;
                meta["source_message_id"] = postUri;
                meta["reply_instructions"] =
                    "Reply using the channels tool with action=reply; post_id and "
                    "root_id are provided by the reply target and filled "
                    "automatically. Text replies are NOT posted. Not every "
                    "mention needs a reply.";
                Json::Value uris(Json::arrayValue);
                uris.append(postUri);
                meta["bluesky_post_uris"] = uris; // session-level dedup
                Json::StreamWriterBuilder wb;
                wb["indentation"] = "";
                std::string metadata = Json::writeString(wb, meta);

                // Sessions are thread-scoped: routing key stays per-post (so the
                // reply target is the specific post) while the session key is
                // the thread root — one session per conversation, not per post.
                DispatchToSession(state, "post:" + postUri, message, "wall", metadata,
                                  "wall:" + state->channel_name + ":" + rootUri);

                processed++;
                if (latestSeen.empty() || indexedAt > latestSeen)
                    latestSeen = indexedAt;
            }

            if (latestSeen != state->bsky_last_seen) {
                state->bsky_last_seen = latestSeen;
                // Persist so restarts don't reprocess old notifications
                if (m_configStore) {
                    m_configStore->Set("",
                        "social." + state->channel_name + ".last_seen",
                        latestSeen);
                }
            }

            // Mark as seen
            {
                Json::Value seenBody;
                seenBody["seenAt"] = iso_now_bsky_cm();

                HttpClient::Request seenReq;
                seenReq.method = "POST";
                seenReq.url = pds + "/xrpc/app.bsky.notification.updateSeen";
                seenReq.headers["Authorization"] = "Bearer " + state->bsky_access_jwt;
                seenReq.headers["Content-Type"] = "application/json";
                seenReq.body = channel_detail::JsonCompact(seenBody);
                m_httpClient.Execute(seenReq);
            }
        } else {
            ALOG_DEBUG("bluesky", "enable_posts disabled, skipping notification poll for " << state->channel_name);
        }

        // --- Chat (DM) polling ---
        bool enableDm = GetString(state->config, "enable_dm") != "false";
        if (enableDm && now >= state->bsky_chat_next_poll) {
            BlueskyChatPollLoop(state);
            state->bsky_chat_next_poll = now + std::chrono::seconds(60);
        }

        state->next_attempt = now + std::chrono::seconds(60);
    }

    ALOG_INFO("bluesky", "poll loop ended for " << state->channel_name);
}

void ChannelManager::BlueskyChatPollLoop(PollerState* state) {
    // Chat endpoints: hit the chat service directly (api.bsky.chat)
    // PDS proxying via atproto-proxy header returns 501 MethodNotImplemented
    // on bsky.social PDS. The chat service accepts the same JWT directly.
    const std::string chatHost = "https://api.bsky.chat";

    // Fetch conversation list
    HttpClient::Request listReq;
    listReq.method = "GET";
    listReq.url = chatHost + "/xrpc/chat.bsky.convo.listConvos?limit=50";
    listReq.headers["Authorization"] = "Bearer " + state->bsky_access_jwt;

    auto listResp = m_httpClient.Execute(listReq);
    ALOG_INFO("bluesky", "chat listConvos status=" << listResp.status_code
              << " body_len=" << listResp.body.size());
    if (listResp.status_code != 200) {
        ALOG_WARNING("bluesky", "chat listConvos failed (" << listResp.status_code
                     << "): " << listResp.body.substr(0, 200));
        return;
    }

    auto listData = ParseJson(listResp.body);
    if (listData.isNull() || !listData.isMember("convos")) {
        ALOG_DEBUG("bluesky", "chat listConvos: no convos field in response");
        return;
    }

    int totalConvos = listData["convos"].size();
    ALOG_INFO("bluesky", "chat listConvos returned " << totalConvos << " conversations");

    for (const auto& convo : listData["convos"]) {
        std::string convoId = GetString(convo, "id");
        int unreadCount = 0;
        if (convo.isMember("unreadCount")) unreadCount = convo["unreadCount"].asInt();
        ALOG_DEBUG("bluesky", "convo " << convoId << " unread=" << unreadCount);
        if (unreadCount == 0) continue;

        // Build DID→handle/display maps from convo members. Membership also
        // drives the 1-1 vs group distinction (Aug 31): Bluesky chats can be
        // group conversations, so chats get channel-style attribution and
        // tool-based replies rather than DM-minimal + auto-delivery.
        std::map<std::string, std::string> didToHandle;
        std::map<std::string, std::string> didToDisplay;
        if (convo.isMember("members") && convo["members"].isArray()) {
            for (const auto& m : convo["members"]) {
                std::string memberDid = GetString(m, "did");
                std::string handle = GetString(m, "handle");
                if (!memberDid.empty() && !handle.empty())
                    didToHandle[memberDid] = handle;
                std::string display = GetString(m, "displayName");
                if (!memberDid.empty() && !display.empty())
                    didToDisplay[memberDid] = display;
            }
        }
        const bool isGroupConvo = (didToHandle.size() > 2);
        std::string groupOthersLabel;
        if (isGroupConvo) {
            for (const auto& [memberDid, handle] : didToHandle) {
                if (memberDid == state->bsky_did) continue; // us
                groupOthersLabel += (groupOthersLabel.empty() ? "@" : ", @") + handle;
            }
        }

        // Fetch messages for this convo
        HttpClient::Request msgReq;
        msgReq.method = "GET";
        msgReq.url = chatHost + "/xrpc/chat.bsky.convo.getMessages?convoId=" + convoId + "&limit=50";
        msgReq.headers["Authorization"] = "Bearer " + state->bsky_access_jwt;

        auto msgResp = m_httpClient.Execute(msgReq);
        if (msgResp.status_code != 200) {
            ALOG_WARNING("bluesky", "chat getMessages failed for " << convoId
                        << " (" << msgResp.status_code << ")");
            continue;
        }

        auto msgData = ParseJson(msgResp.body);
        if (msgData.isNull() || !msgData.isMember("messages")) continue;

        // Process messages: dispatch each new one individually
        std::string maxRev;
        int dispatchCount = 0;

        // Query session metadata for already-processed Bluesky rev IDs
        std::string sessionKey = "chat:" + state->channel_name + ":" + convoId;
        std::set<std::string> seenRevs;
        if (m_sessionQuery) {
            // Query both singular (bluesky_rev) and plural (bluesky_revs) metadata keys
            auto stored = m_sessionQuery(sessionKey, "bluesky_revs");
            for (const auto& r : stored) {
                seenRevs.insert(r);
            }
        }
        // In-memory watermark for same-run dedup
        std::string watermark;
        {
            auto wf = state->bsky_chat_watermarks.find(convoId);
            if (wf != state->bsky_chat_watermarks.end()) watermark = wf->second;
        }

        ALOG_DEBUG("bluesky", "convo " << convoId << " messages="
                  << msgData["messages"].size() << " watermark=\"" << watermark << "\""
                  << " stored_revs=" << seenRevs.size());

        // Auto-reply filter config
        bool autoReply = GetString(state->config, "auto_reply") != "false";
        bool replyToAll = GetString(state->config, "reply_to_all") != "false";

        // Process messages oldest-first (API returns newest-first, so reverse)
        const auto& messages = msgData["messages"];
        std::vector<std::string> newMessages;
        std::vector<std::string> newRevs;
        std::vector<std::string> newSenders;   // sender DIDs
        std::vector<std::string> newHandles;   // sender handles
        std::vector<std::string> newDisplays;  // sender display names

        for (int i = static_cast<int>(messages.size()) - 1; i >= 0; --i) {
            const auto& msg = messages[i];
            std::string rev = GetString(msg, "rev");
            std::string senderDid;

            if (msg.isMember("sender")) senderDid = GetString(msg["sender"], "did");

            // Track max rev across ALL messages (including own)
            if (maxRev.empty() || (!rev.empty() && rev > maxRev)) maxRev = rev;

            // Skip our own messages
            if (senderDid == state->bsky_did) continue;

            // Dedup: in-memory watermark
            if (!watermark.empty() && !rev.empty() && rev <= watermark) {
                continue;
            }

            // Dedup: already stored in session metadata
            if (!rev.empty() && seenRevs.count(rev)) {
                continue;
            }

            std::string text;
            if (msg.isMember("text")) text = GetString(msg, "text");
            if (text.empty()) continue;

            // Resolve sender identity: members map first (getMessages
            // senders carry only a DID), sender fields as fallback.
            std::string senderHandle, senderDisplay;
            auto it = didToHandle.find(senderDid);
            if (it != didToHandle.end()) {
                senderHandle = it->second;
                auto dt = didToDisplay.find(senderDid);
                if (dt != didToDisplay.end()) senderDisplay = dt->second;
            } else if (msg.isMember("sender")) {
                senderHandle = GetString(msg["sender"], "handle");
                senderDisplay = GetString(msg["sender"], "displayName");
            }
            if (senderDisplay.empty()) senderDisplay = senderHandle;
            std::string senderName = senderHandle.empty()
                ? senderDid
                : ("@" + senderHandle); // allowlist matching, unchanged shape

            // Apply auto-reply filter
            bool shouldDispatch = false;
            if (autoReply) {
                if (replyToAll) {
                    shouldDispatch = true;
                } else if (state->config.isMember("reply_to_users") && state->config["reply_to_users"].isArray()) {
                    for (const auto& u : state->config["reply_to_users"]) {
                        if (senderName.find(u.asString()) != std::string::npos) {
                            shouldDispatch = true;
                            break;
                        }
                    }
                }
            }

            if (shouldDispatch) {
                newMessages.push_back(text);
                newRevs.push_back(rev);
                newSenders.push_back(senderDid);
                newHandles.push_back(senderHandle);
                newDisplays.push_back(senderDisplay);
            }
        }

        // Per-message dispatch (Aug 31): every message gets its own arrival
        // row + card, so history attribution is per-sender and the latest
        // arrival's origin always matches the message being answered.
        // Delivery is tool-only everywhere on Bluesky (delivery:"tool"):
        // chats can be groups, so "not addressed to me" must be a real
        // outcome — silence is first-class.
        for (std::size_t i = 0; i < newMessages.size(); ++i) {
            const std::string& text = newMessages[i];
            const std::string& rev = newRevs[i];
            const std::string& senderDid = newSenders[i];
            const std::string& handle = newHandles[i];
            const std::string& display = newDisplays.empty()
                ? std::string() : newDisplays[i];

            // Body: attribution header only (1-1 vs group form), content, no
            // delivery hints — those live in the card.
            std::string header = "Bluesky chat message from " + display;
            if (!handle.empty()) header += " (@" + handle + ")";
            if (isGroupConvo && !groupOthersLabel.empty())
                header += " in group chat with " + groupOthersLabel;
            header += ":";
            std::string message = header + "\n" + text;

            Json::Value meta;
            meta["message_type"] = "chat";
            meta["delivery"] = "tool";
            Json::Value origin;
            if (!handle.empty()) origin["user"] = "@" + handle;
            if (!display.empty() && display != handle && !handle.empty())
                origin["user_display"] = display;
            if (!senderDid.empty())
                origin["user_id"] = senderDid; // did:plc — stable id (#42)
            if (isGroupConvo && !groupOthersLabel.empty())
                origin["channel"] = "group chat with " + groupOthersLabel;
            meta["origin"] = origin;
            if (!rev.empty()) {
                meta["source_message_id"] = rev;
                Json::Value revs(Json::arrayValue);
                revs.append(rev);
                meta["bluesky_revs"] = revs; // session-level dedup
            }
            meta["reply_instructions"] =
                "Reply using the channels tool with action=chat_send; convo_id is "
                "provided by the reply target and filled automatically. Text "
                "replies are NOT delivered. If the message is not addressed to "
                "you, staying silent is correct.";
            Json::StreamWriterBuilder wb;
            wb["indentation"] = "";
            std::string metadata = Json::writeString(wb, meta);

            ALOG_INFO("bluesky", "dispatching chat message from @"
                      << handle << " for convo " << convoId
                      << " (" << state->channel_name
                      << (isGroupConvo ? ", group" : ", 1-1") << ")");
            DispatchToSession(state, "peer:" + convoId, message, "chat", metadata);
            dispatchCount++;
        }

        // Update in-memory watermark (same-run dedup only)
        if (!maxRev.empty()) {
            state->bsky_chat_watermarks[convoId] = maxRev;
            ALOG_DEBUG("bluesky", "convo " << convoId << " watermark=\""
                      << maxRev << "\" dispatched=" << dispatchCount);

            // Mark as read up to maxRev
            Json::Value readBody;
            readBody["convoId"] = convoId;
            readBody["rev"] = maxRev;

            HttpClient::Request readReq;
            readReq.method = "POST";
            readReq.url = chatHost + "/xrpc/chat.bsky.convo.updateRead";
            readReq.headers["Authorization"] = "Bearer " + state->bsky_access_jwt;
            readReq.headers["Content-Type"] = "application/json";
            readReq.body = channel_detail::JsonCompact(readBody);
            m_httpClient.Execute(readReq);
        }
    }
}

void ChannelManager::DispatchToSession(PollerState* state,
                                         const std::string& routingKey,
                                         const std::string& message,
                                         const std::string& sessionType,
                                         const std::string& metadata,
                                         const std::string& explicitSessionKey) {
    bool isPeer = (routingKey.size() > 5 && routingKey.substr(0, 5) == "peer:");
    bool isPost = (routingKey.size() > 5 && routingKey.substr(0, 5) == "post:");

    std::string routingValue = routingKey.substr(5);

    // Look up existing session
    auto entry = m_router.Lookup(state->channel_name, routingKey);

    std::string sessionKey;
    if (entry) {
        sessionKey = entry->session_key;
    } else {
        sessionKey = !explicitSessionKey.empty()
                         ? explicitSessionKey
                         : sessionType + ":" + state->channel_name + ":" + routingValue;
        m_router.Register(state->channel_name, routingKey, sessionKey, state->agent_id);
    }

    // Build reply target
    ReplyTarget replyTarget;
    replyTarget.channel_name = state->channel_name;
    replyTarget.channel_type = state->channel_type;

    bool isThread = (routingKey.size() > 7 && routingKey.substr(0, 7) == "thread:");

    if (isPeer) {
        replyTarget.type = ReplyTarget::Chat;
        replyTarget.peer_id = routingValue;
    } else if (isPost) {
        replyTarget.type = ReplyTarget::Wall;
        size_t commentPos = routingValue.find(":comment:");
        if (commentPos != std::string::npos) {
            replyTarget.post_id = routingValue.substr(0, commentPos);
            replyTarget.reply_to_comment = routingValue.substr(commentPos + 9);
        } else {
            replyTarget.post_id = routingValue;
        }
        replyTarget.group_id = state->group_id;
    } else if (isThread) {
        replyTarget.type = ReplyTarget::Chat;
        replyTarget.email_thread_id = routingValue;
        replyTarget.email_inbox_id = GetString(state->config, "inbox_id");
    }

    m_dispatch(state->agent_id, sessionKey, message, sessionType, replyTarget, metadata);
}


// Minimal HTML stripper for WS email event fallback
static std::string StripHtmlSimple(const std::string& html) {
    std::string out;
    out.reserve(html.size());
    bool inTag = false;
    bool lastWasSpace = true; // trim leading space
    for (char c : html) {
        if (inTag) { if (c == '>') inTag = false; continue; }
        if (c == '<') { inTag = true; continue; }
        if (c == '\n') { if (!lastWasSpace) { out += '\n'; lastWasSpace = true; } continue; }
        if (c == ' ' || c == '\t' || c == '\r') { if (!lastWasSpace) { out += ' '; lastWasSpace = true; } continue; }
        out += c; lastWasSpace = false;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '\n')) out.pop_back();
    return out;
}

// ============================================================================
// Email WebSocket Loop (real-time, with REST polling fallback)
//
// Connects to wss://ws.agentmail.to/v0 and subscribes to message.received
// events. Drogon's WebSocketClient handles reconnection automatically.
// On fatal WS failure, falls back to REST polling (EmailPollLoop).
//
// Architecture:
//   - Each email channel runs in its own thread

} // namespace animus::kernel

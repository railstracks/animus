#include "animus_kernel/ChannelManager.h"

#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <algorithm>
#include <set>
#include <thread>
#include <vector>

#include "animus_kernel/AgentConfigStore.h"
#include "animus_kernel/tools/HttpClient.h"
#include "animus_kernel/interfaces/IrcInterface.h"
#include "animus_kernel/social/TelegramTypes.h"
#include "animus_kernel/social/TelegramBotApi.h"
#include "animus_kernel/whatsapp/WABinary.h"

#include <json/json.h>
#include <json/writer.h>

#include <drogon/WebSocketClient.h>
#include <drogon/HttpRequest.h>
#include <trantor/net/EventLoop.h>

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
    std::string result;
    for (char c : input) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            result += c;
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(c));
            result += buf;
        }
    }
    return result;
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
                               LogCallback logCallback)
    : m_httpClient(httpClient)
    , m_configStore(configStore)
    , m_dispatch(std::move(dispatch))
    , m_logCallback(std::move(logCallback)) {
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

    // Auto-start if enabled
    if (state.enabled && m_running) {
        StartChannel(state);
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

    // Restart the channel with new config
    if (m_running && state.enabled) {
        StopChannel(name);
        state.config = config;
        StartChannel(state);
    }

    // Re-sync credential keys into AgentConfigStore
    SyncChannelCredentialsToConfigStore(name, state.type, config);

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
        it->second.enabled = enabled;
    }

    if (m_configStore) {
        m_configStore->Set("", "channel." + name + ".enabled",
                           enabled ? "true" : "false");
    }

    if (m_running) {
        if (enabled) {
            StartChannel(state);
        } else {
            StopChannel(name);
        }
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

    // Stop all IRC runtimes — extract under lock, stop outside lock to
    // avoid holding m_ircMutex while threads join (prevents deadlock if
    // another thread is waiting on the mutex, e.g. SendIrcPrivmsg).
    std::vector<std::pair<std::string, std::shared_ptr<IrcInterfaceRuntime>>> toStop;
    {
        std::lock_guard<std::mutex> lock(m_ircMutex);
        for (auto& [name, irc] : m_ircRuntimes) {
            if (irc.runtime) {
                toStop.emplace_back(name, irc.runtime);
            }
        }
        m_ircRuntimes.clear();
    }
    for (auto& [name, runtime] : toStop) {
        runtime->Stop();
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
    StopChannel(name);
    if (state.enabled) {
        StartChannel(state);
    }
    return true;
}

// ============================================================================
// Runtime status
// ============================================================================

bool ChannelManager::IsChannelConnected(const std::string& name) const {
    // Check pollers (Telegram, VK, Discord, WhatsApp, Slack, Email)
    {
        std::lock_guard<std::mutex> lock(m_pollersMutex);
        auto it = m_pollers.find(name);
        if (it != m_pollers.end()) {
            return it->second->active && it->second->consecutive_errors == 0;
        }
    }
    // Check IRC
    {
        std::lock_guard<std::mutex> lock(m_ircMutex);
        auto it = m_ircRuntimes.find(name);
        if (it != m_ircRuntimes.end() && it->second.runtime) {
            // IRC doesn't expose IsConnected() — check via last status callback
            return true; // connected flag updated via SyncIrcStatusCallback
        }
    }
    // Check if channel exists in m_channels (Bluesky, Twitter — no poller thread)
    // These channels use Lua adapters for REST operations and don't have
    // a persistent connection. If they're in m_channels, they're "configured"
    // and the Lua adapter handles connectivity.
    {
        std::lock_guard<std::mutex> lock(m_channelsMutex);
        auto it = m_channels.find(name);
        if (it != m_channels.end()) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// SendReply — route auto-reply through the right connector
// ============================================================================

void ChannelManager::SendReply(const ReplyTarget& target, const std::string& text) {
    if (target.channel_type == "irc") {
        SendIrcPrivmsg(target.channel_name, target.irc_target, text);
        return;
    }

    if (target.channel_type == "telegram") {
        ChannelState state;
        {
            std::lock_guard<std::mutex> lock(m_channelsMutex);
            auto it = m_channels.find(target.channel_name);
            if (it == m_channels.end()) {
                std::cerr << "[channels] Telegram reply: channel not found: "
                          << target.channel_name << std::endl;
                return;
            }
            state = it->second;
        }

        std::string token = GetString(state.config, "access_token");
        if (token.empty()) {
            std::cerr << "[channels] Telegram reply: no access token for "
                      << target.channel_name << std::endl;
            return;
        }

        telegram::TelegramBotApi api(m_httpClient);
        int64_t chatId = 0;
        try { chatId = std::stoll(target.peer_id); } catch (...) {
            std::cerr << "[channels] Telegram reply: invalid chat_id: "
                      << target.peer_id << std::endl;
            return;
        }

        telegram::TelegramBotApi::SendMessageOptions opts;
        opts.chat_id = chatId;
        opts.text = text;

        auto msgId = api.SendMessage(token, opts);
        if (msgId) {
            std::cerr << "[channels] Telegram message sent to "
                      << target.peer_id << " (msg_id=" << *msgId << ")" << std::endl;
        } else {
            std::cerr << "[channels] Telegram send failed to "
                      << target.peer_id << std::endl;
        }
        return;
    }

    if (target.channel_type == "vk") {
        ChannelState state;
        {
            std::lock_guard<std::mutex> lock(m_channelsMutex);
            auto it = m_channels.find(target.channel_name);
            if (it == m_channels.end()) return;
            state = it->second;
        }

        std::string token = GetString(state.config, "access_token");
        if (token.empty()) return;

        std::string groupId = GetString(state.config, "group_id");

        if (target.type == ReplyTarget::Chat) {
            std::string body = "peer_id=" + target.peer_id +
                "&message=" + UrlEncode(text) +
                "&random_id=" + std::to_string(rand()) +
                "&access_token=" + token + "&v=5.131";

            HttpClient::Request req;
            req.url = "https://api.vk.ru/method/messages.send";
            req.method = "POST";
            req.headers["Content-Type"] = "application/x-www-form-urlencoded";
            req.body = body;
            req.follow_redirects = false;

            auto resp = m_httpClient.Execute(req);
            std::cerr << "[channels] VK Chat reply: " << resp.status_code
                      << " body=" << resp.body.substr(0, 200) << std::endl;
        } else if (target.type == ReplyTarget::Wall) {
            std::string ownerId = "-" + groupId;
            std::string body = "post_id=" + target.post_id +
                "&owner_id=" + ownerId +
                "&message=" + UrlEncode(text) +
                "&access_token=" + token + "&v=5.131";
            if (!target.reply_to_comment.empty() && target.reply_to_comment != "0") {
                body += "&reply_to_comment=" + target.reply_to_comment;
            }

            HttpClient::Request req;
            req.url = "https://api.vk.ru/method/wall.createComment";
            req.method = "POST";
            req.headers["Content-Type"] = "application/x-www-form-urlencoded";
            req.body = body;
            req.follow_redirects = false;

            auto resp = m_httpClient.Execute(req);
            std::cerr << "[channels] VK Wall reply: " << resp.status_code
                      << " body=" << resp.body.substr(0, 200) << std::endl;
        }
        return;
    }

    if (target.channel_type == "discord") {
        // Channel ID for Discord: peer_id for DMs (peer:CHANNEL_ID), post_id for guild channels (post:CHANNEL_ID)
        std::string channelId;
        if (!target.peer_id.empty()) {
            channelId = target.peer_id;   // DM: peer_id holds the DM channel ID
        } else if (!target.post_id.empty()) {
            channelId = target.post_id;   // Guild channel: post_id holds the channel ID
        }
        std::string botToken;
        {
            std::lock_guard<std::mutex> lock(m_channelsMutex);
            auto it = m_channels.find(target.channel_name);
            if (it != m_channels.end()) {
                botToken = GetString(it->second.config, "bot_token");
            }
        }
        if (botToken.empty()) {
            std::cerr << "[discord] SendReply: no bot_token for " << target.channel_name << std::endl;
            return;
        }

        std::string content = text;
        if (content.size() > 2000) {
            content = content.substr(0, 1997) + "...";
        }

        Json::Value body;
        body["content"] = content;
        // TODO: message_reference for replying to specific messages
        // Requires tracking message_id in ReplyTarget (currently not stored)

        HttpClient::Request req;
        req.method = "POST";
        req.url = "https://discord.com/api/v10/channels/" + channelId + "/messages";
        req.headers["Authorization"] = "Bot " + botToken;
        req.headers["Content-Type"] = "application/json";
        req.follow_redirects = false;  // POST must not follow redirects (causes 405)

        Json::StreamWriterBuilder wb;
        wb.settings_["indentation"] = "";
        req.body = Json::writeString(wb, body);

        HttpClient::Response resp = m_httpClient.Execute(req);
        if (resp.status_code != 200 && resp.status_code != 201) {
            std::cerr << "[discord] SendReply failed (" << resp.status_code << "): "
                      << resp.body.substr(0, 200) << std::endl;
        } else {
            std::cerr << "[discord] SendReply OK to channel " << channelId << std::endl;
        }
        return;  // Discord reply sent, don't fall through
    }
    if (target.channel_type == "whatsapp") {
        // WhatsApp SendReply: queue message for the gateway loop to encrypt and send
        std::cerr << "[whatsapp] SendReply to " << target.peer_id
                  << ": " << text.substr(0, 100) << std::endl;

        // Find the poller state to access the outbox
        std::lock_guard<std::mutex> lock(m_pollersMutex);
        auto it = m_pollers.find(target.channel_name);
        if (it == m_pollers.end()) {
            std::cerr << "[whatsapp] No active poller for " << target.channel_name << std::endl;
            return;
        }
        auto* pState = it->second.get();

        // Recover outbox pointers from poller state config
        if (!pState->config.isMember("_wa_outbox_mutex") || !pState->config.isMember("_wa_outbox")) {
            std::cerr << "[whatsapp] Gateway loop not initialized yet" << std::endl;
            return;
        }

        auto* outboxMutex = reinterpret_cast<std::mutex*>(
            pState->config["_wa_outbox_mutex"].asUInt64());
        auto* outbox = reinterpret_cast<std::vector<OutboundMessage>*>(
            pState->config["_wa_outbox"].asUInt64());

        if (!outboxMutex || !outbox) {
            std::cerr << "[whatsapp] Invalid outbox pointers" << std::endl;
            return;
        }

        {
            std::lock_guard<std::mutex> olk(*outboxMutex);
            OutboundMessage msg;
            msg.baseJid = target.peer_id;  // Base JID for message routing
            msg.jid = target.peer_id;      // Default: same as base (will be resolved to device JID in send)
            msg.text = text;
            msg.is_group = animus::whatsapp::isJidGroup(target.peer_id);
            outbox->push_back(std::move(msg));
        }
        std::cerr << "[whatsapp] Queued message for " << target.peer_id << std::endl;
        return;
    }

    if (target.channel_type == "nextcloud") {
        ChannelState state;
        {
            std::lock_guard<std::mutex> lock(m_channelsMutex);
            auto it = m_channels.find(target.channel_name);
            if (it == m_channels.end()) return;
            state = it->second;
        }

        std::string serverUrl = GetString(state.config, "server_url");
        std::string username = GetString(state.config, "username");
        std::string appPassword = GetString(state.config, "app_password");

        if (serverUrl.empty() || username.empty() || appPassword.empty()) {
            std::cerr << "[nextcloud] SendReply: missing server_url/username/app_password" << std::endl;
            return;
        }

        // POST /ocs/v2.php/apps/spreed/api/v1/chat/{token}
        std::string url = serverUrl + "/ocs/v2.php/apps/spreed/api/v1/chat/" + target.peer_id;

        Json::Value body;
        body["message"] = text;
        body["silent"] = false;

        Json::StreamWriterBuilder wb;
        wb.settings_["indentation"] = "";

        HttpClient::Request req;
        req.method = "POST";
        req.url = url;
        req.headers["Content-Type"] = "application/json";
        req.headers["Accept"] = "application/json";
        req.headers["OCS-APIREQUEST"] = "true";
        req.headers["Authorization"] = "Basic " + Base64EncodeStr(username + ":" + appPassword);
        req.body = Json::writeString(wb, body);
        req.follow_redirects = false;

        auto resp = m_httpClient.Execute(req);
        if (resp.status_code != 200 && resp.status_code != 201) {
            std::cerr << "[nextcloud] SendReply failed (" << resp.status_code
                      << "): " << resp.body.substr(0, 200) << std::endl;
        } else {
            std::cerr << "[nextcloud] SendReply OK to conversation " << target.peer_id << std::endl;
        }
        return;
    }

    if (target.channel_type == "email") {
        ChannelState state;
        {
            std::lock_guard<std::mutex> lock(m_channelsMutex);
            auto it = m_channels.find(target.channel_name);
            if (it == m_channels.end()) return;
            state = it->second;
        }

        std::string apiKey = GetString(state.config, "api_key");
        std::string inboxId = target.email_inbox_id.empty()
            ? GetString(state.config, "inbox_id")
            : target.email_inbox_id;
        if (apiKey.empty() || inboxId.empty()) {
            std::cerr << "[channels] Email reply: missing api_key or inbox_id" << std::endl;
            return;
        }

        // Build reply payload
        Json::Value payload(Json::objectValue);
        payload["text"] = text;
        if (!target.email_thread_id.empty()) {
            payload["thread_id"] = target.email_thread_id;
        }

        Json::StreamWriterBuilder wb;
        wb.settings_["indentation"] = "";
        std::string bodyStr = Json::writeString(wb, payload);

        HttpClient::Request req;
        req.method = "POST";
        req.url = std::string("https://api.agentmail.to/v0/inboxes/") + inboxId + "/messages/send";
        req.headers["Authorization"] = "Bearer " + apiKey;
        req.headers["Content-Type"] = "application/json";
        req.body = bodyStr;

        auto resp = m_httpClient.Execute(req);
        std::cerr << "[channels] Email reply: " << resp.status_code
                  << " to thread=" << target.email_thread_id << std::endl;
        return;
    }

    if (target.channel_type == "slack") {
        // Slack auto-reply: send via chat.postMessage
        std::string channelId = target.peer_id;
        if (channelId.empty()) channelId = target.post_id;
        if (channelId.empty()) {
            std::cerr << "[slack] SendReply: no channel_id in reply target" << std::endl;
            return;
        }

        std::string botToken;
        {
            std::lock_guard<std::mutex> lock(m_channelsMutex);
            auto it = m_channels.find(target.channel_name);
            if (it != m_channels.end()) {
                botToken = GetString(it->second.config, "bot_token");
            }
        }
        if (botToken.empty()) {
            std::cerr << "[slack] SendReply: no bot_token for " << target.channel_name << std::endl;
            return;
        }

        // Build JSON body
        Json::Value body;
        body["channel"] = channelId;
        body["text"] = text;
        // Thread support: if reply_to_comment holds a thread_ts, thread the reply
        if (!target.reply_to_comment.empty()) {
            body["thread_ts"] = target.reply_to_comment;
        }

        HttpClient::Request req;
        req.method = "POST";
        req.url = "https://slack.com/api/chat.postMessage";
        req.headers["Authorization"] = "Bearer " + botToken;
        req.headers["Content-Type"] = "application/json; charset=utf-8";
        req.follow_redirects = false;

        Json::StreamWriterBuilder wb;
        wb.settings_["indentation"] = "";
        req.body = Json::writeString(wb, body);

        HttpClient::Response resp = m_httpClient.Execute(req);
        if (resp.status_code != 200) {
            std::cerr << "[slack] SendReply failed (HTTP " << resp.status_code << "): "
                      << resp.body.substr(0, 200) << std::endl;
        } else {
            // Slack returns 200 even on API errors — check {ok: true}
            Json::Value rdata;
            Json::CharReaderBuilder crb;
            std::istringstream iss(resp.body);
            std::string errs;
            if (Json::parseFromStream(crb, iss, &rdata, &errs) && rdata.isMember("ok")) {
                if (rdata["ok"].asBool()) {
                    std::cerr << "[slack] SendReply OK to " << channelId << std::endl;
                } else {
                    std::cerr << "[slack] SendReply API error: "
                              << (rdata.isMember("error") ? rdata["error"].asString() : "unknown")
                              << std::endl;
                }
            }
        }
        return;
    }

    if (target.channel_type == "twitter") {
        // Twitter auto-reply: routed through Lua social tool, not direct API call.
        // The agent session produces a response → SendAutoReply → this path.
        // For now, log and skip — full implementation with polling comes later.
        std::cerr << "[channels] Twitter auto-reply not yet wired: channel="
                  << target.channel_name << " peer=" << target.peer_id << std::endl;
        return;
    }

    std::cerr << "[channels] SendReply: unsupported type: "
              << target.channel_type << std::endl;
}

// ============================================================================
// IRC
// ============================================================================

bool ChannelManager::SendIrcPrivmsg(const std::string& channelName,
                                     const std::string& target,
                                     const std::string& message) {
    std::lock_guard<std::mutex> lock(m_ircMutex);
    auto it = m_ircRuntimes.find(channelName);
    if (it == m_ircRuntimes.end() || !it->second.runtime) return false;
    return it->second.runtime->SendPrivmsg(target, message);
}

void ChannelManager::StartIrcChannel(const ChannelState& state) {
    const Json::Value& cfg = state.config;

    IrcInterfaceRuntime::Config ircCfg;
    ircCfg.host = GetString(cfg, "host");
    ircCfg.port = GetInt(cfg, "port", 6667);
    ircCfg.serverPassword = GetString(cfg, "server_password");
    ircCfg.nick = GetString(cfg, "nick");
    ircCfg.username = GetString(cfg, "username", "animus");
    ircCfg.realname = GetString(cfg, "realname", "Animus Agent");

    // Channels
    if (cfg.isMember("channels") && cfg["channels"].isArray()) {
        for (const auto& ch : cfg["channels"]) {
            IrcInterfaceRuntime::ChannelConfig entry;
            entry.name = GetString(ch, "name");
            entry.key = GetString(ch, "key");
            ircCfg.channels.push_back(entry);
        }
    }

    // Reconnect
    if (cfg.isMember("reconnect") && cfg["reconnect"].isObject()) {
        ircCfg.reconnectEnabled = cfg["reconnect"].get("enabled", true).asBool();
        ircCfg.reconnectInitialDelayMs = cfg["reconnect"].get("initial_delay_ms", 1000).asInt();
        ircCfg.reconnectMaxDelayMs = cfg["reconnect"].get("max_delay_ms", 60000).asInt();
    }

    // TLS
    ircCfg.useTls = cfg.get("use_tls", false).asBool();
    // Force IPv4
    ircCfg.forceIpv4 = cfg.get("force_ipv4", true).asBool();

    auto runtime = std::make_shared<IrcInterfaceRuntime>(
        state.name,
        ircCfg,
        [this, name = state.name, botNick = ircCfg.nick](
            const std::string& nick,
            const std::string& target,
            const std::string& message,
            bool isNotice) {
            SyncIrcMessageCallback(name, botNick, nick, target, message, isNotice);
        },
        [this, name = state.name](bool connected, const std::string& error) {
            SyncIrcStatusCallback(name, connected, 0);
        });

    {
        std::lock_guard<std::mutex> lock(m_ircMutex);
        m_ircRuntimes[state.name] = IrcRuntime{state.name, runtime};
    }

    runtime->Start();
    std::cerr << "[channels] IRC channel started: " << state.name
              << " (" << ircCfg.host << ":" << ircCfg.port << ")" << std::endl;
}

void ChannelManager::StopIrcChannel(const std::string& name) {
    // Extract runtime under lock, then stop outside lock to avoid blocking
    // other IRC operations while the thread joins (which may take up to 2s
    // in poll(), or longer if DNS/connect is stuck).
    std::shared_ptr<IrcInterfaceRuntime> runtime;
    {
        std::lock_guard<std::mutex> lock(m_ircMutex);
        auto it = m_ircRuntimes.find(name);
        if (it == m_ircRuntimes.end() || !it->second.runtime) return;
        runtime = it->second.runtime;
        m_ircRuntimes.erase(it);
    }
    runtime->Stop();
    std::cerr << "[channels] IRC channel stopped: " << name << std::endl;
}

void ChannelManager::SyncIrcMessageCallback(
    const std::string& channelName,
    const std::string& botNick,
    const std::string& sourceNick,
    const std::string& target,
    const std::string& message,
    bool isNotice) {

    if (sourceNick.empty() || message.empty()) return;
    if (sourceNick == botNick) return;

    // Load channel config for filtering
    ChannelState state;
    {
        std::lock_guard<std::mutex> lock(m_channelsMutex);
        auto it = m_channels.find(channelName);
        if (it == m_channels.end() || !it->second.enabled) return;
        state = it->second;
    }

    const Json::Value& cfg = state.config;
    bool respondToChannel = cfg.get("respond_to_channel_activity", true).asBool();
    bool respondToDm = cfg.get("respond_to_direct_messages", true).asBool();
    bool respondToNotices = cfg.get("respond_to_notices", false).asBool();

    if (isNotice && !respondToNotices) return;

    const bool channelMessage = !target.empty() &&
        (target[0] == '#' || target[0] == '&' || target[0] == '!' || target[0] == '+');
    if (channelMessage && !respondToChannel) return;
    if (!channelMessage && !respondToDm) return;

    // Allowed DM users filter
    if (!channelMessage && cfg.isMember("allowed_dm_users") && cfg["allowed_dm_users"].isArray()) {
        auto arr = cfg["allowed_dm_users"];
        if (arr.size() > 0) {
            bool found = false;
            for (Json::ArrayIndex i = 0; i < arr.size(); ++i) {
                if (arr[i].isString() && arr[i].asString() == sourceNick) {
                    found = true;
                    break;
                }
            }
            if (!found) return;
        }
    }

    std::string conversationId = channelMessage
        ? ("channel:" + target) : ("dm:" + sourceNick);

    std::string agentId = GetString(cfg, "agent_id", "");

    // Build reply target
    ReplyTarget replyTarget;
    replyTarget.channel_name = channelName;
    replyTarget.channel_type = "irc";
    replyTarget.type = ReplyTarget::Chat;
    replyTarget.irc_target = channelMessage ? target : sourceNick;
    replyTarget.interface_name = channelName;

    std::string sessionType = "irc";
    std::string sessionKey = sessionType + ":" + channelName + ":" + conversationId;
    std::string prompt;
    const std::string replyHint = "\n\nYou are responding via IRC. Respond naturally — your reply will be sent automatically. Do NOT use the social tool to reply.";
    if (channelMessage) {
        prompt = "IRC message from " + sourceNick + " in " + target + ":\n" + message + replyHint;
    } else {
        prompt = "IRC private message from " + sourceNick + ":\n" + message + replyHint;
    }

    m_dispatch(agentId, sessionKey, prompt, sessionType, replyTarget);
}

void ChannelManager::SyncIrcStatusCallback(
    const std::string& channelName,
    bool connected,
    std::uint64_t eventUnixMs) {
    std::lock_guard<std::mutex> lock(m_channelsMutex);
    auto it = m_channels.find(channelName);
    if (it != m_channels.end()) {
        it->second.connected = connected;
        it->second.lastEventUnixMs = eventUnixMs;
    }
}

// ============================================================================
// StartChannel / StopChannel — dispatch to the right connector type
// ============================================================================

void ChannelManager::StartChannel(const ChannelState& state) {
    if (state.type == "irc") {
        StartIrcChannel(state);
    } else if (state.type == "vk" || state.type == "telegram" || state.type == "bluesky" || state.type == "twitter") {
        // Poller-based connectors
        auto poller = std::make_unique<PollerState>();
        poller->channel_name = state.name;
        poller->channel_type = state.type;
        poller->config = state.config;
        poller->next_attempt = std::chrono::steady_clock::now();

        // Agent association (which agent to dispatch events to)
        poller->agent_id = GetString(state.config, "agent_id", "");

        // Load persisted state from config
        if (m_configStore) {
            // Telegram: restore last_update_id
            std::string stored = m_configStore->Get("",
                "channel." + state.name + ".polling.last_update_id");
            if (!stored.empty()) {
                try { poller->last_update_id = std::stoll(stored); } catch (...) {}
            }

            // VK: restore long poll state
            poller->lp_ts = m_configStore->Get("",
                "channel." + state.name + ".polling.ts");
            poller->lp_key = m_configStore->Get("",
                "channel." + state.name + ".polling.key");
            poller->lp_server = m_configStore->Get("",
                "channel." + state.name + ".polling.server");
        }

        poller->active = true;

        std::string name = state.name;
        if (state.type == "telegram") {
            poller->thread = std::thread(&ChannelManager::TelegramLongPollLoop, this, poller.get());
        } else if (state.type == "vk") {
            poller->thread = std::thread(&ChannelManager::VkLongPollLoop, this, poller.get());
        } else {
            // Bluesky / Twitter — REST polling (TODO)
            std::cerr << "[channels] REST polling not yet implemented for "
                      << state.type << std::endl;
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_pollersMutex);
            m_pollers[name] = std::move(poller);
        }

        std::cerr << "[channels] Started " << state.type << " poller: " << state.name << std::endl;
    } else if (state.type == "email") {
        // Email — WebSocket (real-time) with REST polling fallback
        auto poller = std::make_unique<PollerState>();
        poller->channel_name = state.name;
        poller->channel_type = state.type;
        poller->config = state.config;
        poller->agent_id = GetString(state.config, "agent_id", "");
        poller->next_attempt = std::chrono::steady_clock::now();

        // Restore last seen event timestamp (for polling fallback)
        if (m_configStore) {
            poller->lp_ts = m_configStore->Get(poller->agent_id,
                "channel." + state.name + ".polling.last_before");
        }

        poller->active = true;

        std::string name = state.name;
        poller->thread = std::thread(&ChannelManager::EmailWebSocketLoop, this, poller.get());

        {
            std::lock_guard<std::mutex> lock(m_pollersMutex);
            m_pollers[name] = std::move(poller);
        }

        std::cerr << "[channels] Started email connector (WebSocket): " << state.name << std::endl;
    } else if (state.type == "discord") {
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

        std::cerr << "[channels] Started Discord Gateway: " << state.name << std::endl;
    } else if (state.type == "whatsapp") {
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

        std::cerr << "[channels] Started WhatsApp Gateway: " << state.name << std::endl;
    } else if (state.type == "slack") {
        // Slack: Socket Mode (Phase 2) when app_token is available,
        // REST polling (Phase 1) as fallback.
        auto poller = std::make_unique<PollerState>();
        poller->channel_name = state.name;
        poller->channel_type = state.type;
        poller->config = state.config;
        poller->agent_id = GetString(state.config, "agent_id", "");
        poller->next_attempt = std::chrono::steady_clock::now();
        poller->active = true;

        // Restore last-seen message timestamp for polling watermark
        if (m_configStore) {
            poller->lp_ts = m_configStore->Get("",
                "channel." + state.name + ".polling.latest_ts");
        }

        std::string name = state.name;

        // Use Socket Mode if app_token is configured, otherwise fall back to REST polling
        std::string appToken = GetString(state.config, "app_token");
        if (!appToken.empty()) {
            poller->thread = std::thread(&ChannelManager::SlackSocketModeLoop, this, poller.get());
            std::cerr << "[channels] Started Slack Socket Mode: " << state.name << std::endl;
        } else {
            poller->thread = std::thread(&ChannelManager::SlackPollingLoop, this, poller.get());
            std::cerr << "[channels] Started Slack polling (no app_token): " << state.name << std::endl;
        }

        {
            std::lock_guard<std::mutex> lock(m_pollersMutex);
            m_pollers[name] = std::move(poller);
        }
    } else if (state.type == "nextcloud") {
        // Nextcloud Talk — OCS API long-polling
        auto poller = std::make_unique<PollerState>();
        poller->channel_name = state.name;
        poller->channel_type = state.type;
        poller->config = state.config;
        poller->agent_id = GetString(state.config, "agent_id", "");
        poller->next_attempt = std::chrono::steady_clock::now();
        poller->active = true;

        // Restore last-known message IDs from config store
        // (per-conversation watermarks stored as channel.{name}.polling.{token}.last_msg_id)
        if (m_configStore) {
            poller->lp_ts = m_configStore->Get("",
                "channel." + state.name + ".polling.last_room_sync");
        }

        std::string name = state.name;
        poller->thread = std::thread(&ChannelManager::NextcloudTalkPollLoop, this, poller.get());

        {
            std::lock_guard<std::mutex> lock(m_pollersMutex);
            m_pollers[name] = std::move(poller);
        }

        std::cerr << "[channels] Started Nextcloud Talk poller: " << state.name << std::endl;
    } else {
        std::cerr << "[channels] Unknown channel type: " << state.type << std::endl;
    }
}

void ChannelManager::StopChannel(const std::string& name) {
    // Stop IRC
    StopIrcChannel(name);

    // Stop poller
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

        std::cerr << "[channels] Found channel: " << name << " type=" << type
                  << " enabled=" << enabledStr << std::endl;
        m_channels[name] = state;
    }

    std::cerr << "[channels] Loaded " << m_channels.size() << " channels from config store" << std::endl;
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

        std::cerr << "[channels] Migrated social instance: " << platformId << std::endl;
        migrated++;
    }

    // --- Migrate interfaces.json ---
    // TODO: Read interfaces.json file, write to config store, rename file

    if (migrated > 0) {
        std::cerr << "[channels] Migrated " << migrated << " legacy social instances" << std::endl;
    }
    return migrated;
}

// ============================================================================
// Telegram Long Poll Loop
// ============================================================================

void ChannelManager::TelegramLongPollLoop(PollerState* state) {
    std::cerr << "[channels:telegram] Long Poll loop starting for "
              << state->channel_name << std::endl;

    std::string token = GetString(state->config, "access_token");
    if (token.empty()) {
        std::cerr << "[channels:telegram] No access token for "
                  << state->channel_name << std::endl;
        state->active = false;
        return;
    }

    telegram::TelegramBotApi api(m_httpClient);

    // Verify bot token with getMe
    auto botInfo = api.GetMe(token);
    if (!botInfo) {
        std::cerr << "[channels:telegram] getMe failed for " << state->channel_name
                  << " — check bot token" << std::endl;
        state->active = false;
        return;
    }
    std::cerr << "[channels:telegram] Connected as @" << botInfo->username
              << " (" << botInfo->first_name << ", id=" << botInfo->id << ")" << std::endl;

    while (state->active && !m_stopRequested) {
        try {
            auto now = std::chrono::steady_clock::now();
            if (now < state->next_attempt) {
                auto sleepMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    state->next_attempt - now).count();
                std::this_thread::sleep_for(std::chrono::milliseconds(
                    std::min(sleepMs, (int64_t)5000)));
                continue;
            }

            // Long poll
            telegram::TelegramBotApi::GetUpdatesOptions opts;
            opts.offset = (state->last_update_id > 0) ? state->last_update_id + 1 : 0;
            opts.timeout = GetInt(state->config, "polling.wait", 25);

            auto updates = api.GetUpdates(token, opts);
            if (m_stopRequested) break;

            for (auto& update : updates) {
                state->last_update_id = std::max(state->last_update_id, update.update_id);

                switch (update.type) {
                    case telegram::Update::Type::Message:
                    case telegram::Update::Type::EditedMessage:
                    case telegram::Update::Type::ChannelPost:
                    case telegram::Update::Type::EditedChannelPost:
                        ProcessTelegramMessage(state, update.message);
                        break;
                    case telegram::Update::Type::MyChatMember:
                        ProcessTelegramChatMemberUpdate(state, update);
                        break;
                    default:
                        break;
                }
            }

            // Persist last_update_id
            if (!updates.empty() && m_configStore) {
                m_configStore->Set("", "channel." + state->channel_name + ".polling.last_update_id",
                                   std::to_string(state->last_update_id));
            }

            state->consecutive_errors = 0;
        } catch (const std::exception& ex) {
            std::cerr << "[channels:telegram] Exception for "
                      << state->channel_name << ": " << ex.what() << std::endl;
            state->consecutive_errors++;
            int backoff = std::min(60, state->consecutive_errors * 5);
            state->next_attempt = std::chrono::steady_clock::now() + std::chrono::seconds(backoff);
        }
    }

    std::cerr << "[channels:telegram] Loop ended for " << state->channel_name << std::endl;
}

void ChannelManager::ProcessTelegramMessage(
        PollerState* state,
        const telegram::Message& msg) {
    if (msg.text.empty()) return;

    std::string routingKey = "peer:" + std::to_string(msg.chat.id);
    if (msg.message_thread_id > 0) {
        routingKey += ":" + std::to_string(msg.message_thread_id);
    }

    std::string senderName = msg.from.DisplayName();
    std::string chatName = msg.chat.DisplayName();
    std::string prompt;

    const std::string replyHint = "\n\nYou are responding via Telegram. Respond naturally — your reply will be sent automatically. Do NOT use the social tool to reply.";

    if (msg.chat.IsPrivate()) {
        prompt = "New Telegram message from " + senderName + ":\n" + msg.text + replyHint;
    } else {
        prompt = "Telegram message from " + senderName + " in " + chatName;
        if (msg.message_thread_id > 0) prompt += " (topic thread)";
        prompt += ":\n" + msg.text + replyHint;
    }

    std::string sessionType = msg.chat.IsPrivate() ? "telegram:private" : "telegram:group";

    DispatchToSession(state, routingKey, prompt, sessionType);
}

void ChannelManager::ProcessTelegramChatMemberUpdate(
        PollerState* state,
        const telegram::Update& update) {
    std::string status = update.member_status;
    std::string chatId = std::to_string(update.member_chat_id);

    if (status == "member" || status == "administrator") {
        std::cerr << "[channels:telegram] Bot added to chat " << chatId
                  << " (status: " << status << ")" << std::endl;
    } else if (status == "left" || status == "kicked") {
        std::cerr << "[channels:telegram] Bot removed from chat " << chatId
                  << " (status: " << status << ")" << std::endl;
    }
}

// ============================================================================
// VK Long Poll Loop
// ============================================================================

void ChannelManager::VkLongPollLoop(PollerState* state) {
    std::cerr << "[channels:vk] Long Poll loop starting for "
              << state->channel_name << std::endl;

    std::string token = GetString(state->config, "access_token");
    if (token.empty()) {
        std::cerr << "[channels:vk] No access token for "
                  << state->channel_name << std::endl;
        state->active = false;
        return;
    }

    state->group_id = GetString(state->config, "group_id");

    // Fetch Long Poll server if not restored from persisted state
    if (state->lp_server.empty()) {
        if (!FetchVkLongPollServer(state)) {
            std::cerr << "[channels:vk] Failed to get Long Poll server for "
                      << state->channel_name << std::endl;
            state->active = false;
            return;
        }
    }

    while (state->active && !m_stopRequested) {
        auto now = std::chrono::steady_clock::now();
        if (now < state->next_attempt) {
            auto sleepMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                state->next_attempt - now).count();
            std::this_thread::sleep_for(std::chrono::milliseconds(
                std::min(sleepMs, (int64_t)60000)));
            continue;
        }

        // Build Long Poll URL
        std::string url = BuildVkLongPollUrl(state);
        int waitSeconds = GetInt(state->config, "polling.wait", 25);

        HttpClient::Request req;
        req.url = url + "&wait=" + std::to_string(waitSeconds) + "&mode=2";
        req.timeout_seconds = waitSeconds + 15;

        auto resp = m_httpClient.Execute(req);

        if (m_stopRequested) break;

        if (resp.status_code != 200) {
            state->consecutive_errors++;
            state->next_attempt = now + std::chrono::seconds(30);
            continue;
        }

        auto json = ParseJson(resp.body);

        // Check for expired key — refresh
        if (json.isMember("failed")) {
            int failed = json.get("failed", 0).asInt();
            if (failed == 1) {
                state->lp_ts = GetString(json, "ts", state->lp_ts);
                continue;
            } else {
                if (!FetchVkLongPollServer(state)) {
                    state->consecutive_errors++;
                    state->next_attempt = now + std::chrono::seconds(30);
                    continue;
                }
                continue;
            }
        }

        state->consecutive_errors = 0;
        state->lp_ts = GetString(json, "ts", state->lp_ts);

        // Persist ts
        if (m_configStore) {
            m_configStore->Set("", "channel." + state->channel_name + ".polling.ts",
                               state->lp_ts);
        }

        auto& updates = json["updates"];
        if (updates.isArray()) {
            for (const auto& update : updates) {
                try {
                    std::string eventType = GetString(update, "type");
                    if (eventType.empty()) continue;

                    Json::StreamWriterBuilder wb;
                    wb.settings_["indentation"] = "";
                    std::string objectJson = Json::writeString(wb, update["object"]);

                    ProcessVKEvent(state, eventType, objectJson);
                } catch (const std::exception& ex) {
                    std::cerr << "[channels:vk] Exception processing event: "
                              << ex.what() << std::endl;
                }
            }
        }

        // Prune expired sessions
        m_router.PruneExpired(std::chrono::seconds(86400));
    }

    std::cerr << "[channels:vk] Long Poll loop ended for "
              << state->channel_name << std::endl;
}

// --- VK event processing ---

void ChannelManager::ProcessVKEvent(PollerState* state,
                                     const std::string& eventType,
                                     const std::string& objectJson) {
    if (eventType == "message_new") {
        ProcessVKMessageNew(state, objectJson);
    } else if (eventType == "wall_post_new") {
        ProcessVKWallPostNew(state, objectJson);
    } else if (eventType == "wall_reply_new") {
        ProcessVKWallReplyNew(state, objectJson);
    }
}

void ChannelManager::ProcessVKMessageNew(PollerState* state,
                                          const std::string& objectJson) {
    auto obj = ParseJson(objectJson);
    if (obj.isNull()) return;

    auto& msg = obj.isMember("message") ? obj["message"] : obj;

    std::string peerId = std::to_string(GetInt(msg, "peer_id"));
    std::string fromId = std::to_string(GetInt(msg, "from_id"));
    std::string text = GetString(msg, "text");
    int64_t messageId = GetInt(msg, "id");

    // Skip own messages
    int64_t groupId = 0;
    try { groupId = std::stoll(state->group_id); } catch (...) {}
    int64_t fromIdVal = std::stoll(fromId);
    if (fromIdVal == -groupId || fromIdVal == groupId) return;

    // Deduplicate
    if (state->seenEventIds.count(messageId)) return;
    state->seenEventIds.insert(messageId);
    if (state->seenEventIds.size() > PollerState::kMaxSeenIds) {
        state->seenEventIds.erase(state->seenEventIds.begin());
    }

    std::cerr << "[channels:vk] message_new: peer=" << peerId
              << " from=" << fromId << " text='" << text << "'" << std::endl;

    auto names = ResolveVKUsers(state, {fromId});
    std::string displayName = names.count(fromId) ? names[fromId] : fromId;

    std::string formattedMsg = displayName + " (" + fromId + "): " + text;
    std::string history = FetchVKChatHistory(state, peerId, 10);

    std::string replyHint = "\n\nYou are responding via VK chat. Respond naturally — your reply will be sent automatically. Do NOT use the social tool to reply.";
    std::string prompt;
    if (history.empty()) {
        prompt = "New message in your VK community chat (channel: " +
                 state->channel_name + ", peer_id: " + peerId + "):\n\n" +
                 formattedMsg + replyHint;
    } else {
        prompt = "New message in your VK community chat (channel: " +
                 state->channel_name + ", peer_id: " + peerId + "):\n\n"
                 "--- Recent conversation ---\n" + history +
                 "\n--- End history ---\n\nNew message:\n" + formattedMsg + replyHint;
    }

    DispatchToSession(state, "peer:" + peerId, prompt, "chat");
}

void ChannelManager::ProcessVKWallPostNew(PollerState* state,
                                           const std::string& objectJson) {
    auto obj = ParseJson(objectJson);
    if (obj.isNull()) return;

    std::string postId = std::to_string(GetInt(obj, "id"));
    std::string fromId = std::to_string(GetInt(obj, "from_id"));
    std::string text = GetString(obj, "text");

    int64_t groupId = 0;
    try { groupId = std::stoll(state->group_id); } catch (...) {}
    int64_t fromIdVal = std::stoll(fromId);
    if (fromIdVal == -groupId || fromIdVal == groupId) return;

    int64_t postIdInt = GetInt(obj, "id");
    if (state->seenEventIds.count(postIdInt)) return;
    state->seenEventIds.insert(postIdInt);
    if (state->seenEventIds.size() > PollerState::kMaxSeenIds) {
        state->seenEventIds.erase(state->seenEventIds.begin());
    }

    auto names = ResolveVKUsers(state, {fromId});
    std::string displayName = names.count(fromId) ? names[fromId] : fromId;

    std::string prompt = "New post on your VK community wall:\n\n" +
        displayName + " (" + fromId + ") posted:\n" + text +
        "\n\nYou are responding via VK wall. Respond naturally — your reply will be posted as a comment automatically. Do NOT use the social tool to reply.";

    DispatchToSession(state, "post:" + postId, prompt, "wall");
}

void ChannelManager::ProcessVKWallReplyNew(PollerState* state,
                                            const std::string& objectJson) {
    auto obj = ParseJson(objectJson);
    if (obj.isNull()) return;

    std::string postId = std::to_string(GetInt(obj, "post_id"));
    std::string fromId = std::to_string(GetInt(obj, "from_id"));
    std::string text = GetString(obj, "text");
    std::string replyToComment = std::to_string(GetInt(obj, "reply_to_comment"));

    int64_t groupId = 0;
    try { groupId = std::stoll(state->group_id); } catch (...) {}
    int64_t fromIdVal = std::stoll(fromId);
    if (fromIdVal == -groupId || fromIdVal == groupId) return;

    int64_t replyIdInt = GetInt(obj, "id");
    if (state->seenEventIds.count(replyIdInt)) return;
    state->seenEventIds.insert(replyIdInt);
    if (state->seenEventIds.size() > PollerState::kMaxSeenIds) {
        state->seenEventIds.erase(state->seenEventIds.begin());
    }

    auto names = ResolveVKUsers(state, {fromId});
    std::string displayName = names.count(fromId) ? names[fromId] : fromId;

    std::string prompt = "New comment on your VK community wall post (post_id=" +
        postId + "):\n\n" +
        displayName + " (" + fromId + "): " + text +
        "\n\nYou are responding via VK wall. Respond naturally — your reply will be posted as a comment automatically. Do NOT use the social tool to reply.";

    std::string routingKey = "post:" + postId;
    if (!replyToComment.empty() && replyToComment != "0") {
        routingKey = "post:" + postId + ":comment:" + replyToComment;
    }

    DispatchToSession(state, routingKey, prompt, "wall");
}

// --- VK helpers ---

bool ChannelManager::FetchVkLongPollServer(PollerState* state) {
    std::string token = GetString(state->config, "access_token");
    std::string url = "https://api.vk.ru/method/groups.getLongPollServer?"
        "group_id=" + state->group_id +
        "&v=5.131&access_token=" + token;

    HttpClient::Request req;
    req.url = url;
    req.timeout_seconds = 10;

    auto resp = m_httpClient.Execute(req);
    if (resp.status_code != 200) return false;

    auto json = ParseJson(resp.body);
    if (json.isMember("error")) return false;

    auto& response = json["response"];
    state->lp_server = GetString(response, "server");
    state->lp_key = GetString(response, "key");
    state->lp_ts = GetString(response, "ts");

    if (state->lp_server.empty() || state->lp_key.empty()) return false;

    // Persist
    if (m_configStore) {
        m_configStore->Set("", "channel." + state->channel_name + ".polling.server",
                           state->lp_server);
        m_configStore->Set("", "channel." + state->channel_name + ".polling.key",
                           state->lp_key);
        m_configStore->Set("", "channel." + state->channel_name + ".polling.ts",
                           state->lp_ts);
    }

    return true;
}

std::string ChannelManager::BuildVkLongPollUrl(const PollerState* state) const {
    return state->lp_server + "?act=a_check&key=" + state->lp_key +
           "&ts=" + state->lp_ts;
}

std::unordered_map<std::string, std::string>
ChannelManager::ResolveVKUsers(PollerState* state,
                                const std::vector<std::string>& userIds) {
    std::unordered_map<std::string, std::string> result;
    if (userIds.empty()) return result;

    std::string token = GetString(state->config, "access_token");
    std::string ids;
    for (size_t i = 0; i < userIds.size(); ++i) {
        if (i > 0) ids += ",";
        ids += userIds[i];
    }

    std::string url = "https://api.vk.ru/method/users.get?"
        "user_ids=" + ids +
        "&fields=first_name,last_name"
        "&v=5.131&access_token=" + token;

    HttpClient::Request req;
    req.url = url;
    req.timeout_seconds = 10;

    auto resp = m_httpClient.Execute(req);
    if (resp.status_code != 200) return result;

    auto json = ParseJson(resp.body);
    if (json.isMember("error")) return result;

    auto& response = json["response"];
    if (response.isArray()) {
        for (const auto& user : response) {
            std::string uid = std::to_string(GetInt(user, "id"));
            result[uid] = GetString(user, "first_name") + " " + GetString(user, "last_name");
        }
    }

    return result;
}

std::string ChannelManager::FetchVKChatHistory(PollerState* state,
                                                 const std::string& peerId,
                                                 int count) {
    std::string token = GetString(state->config, "access_token");
    std::string url = "https://api.vk.ru/method/messages.getHistory?"
        "peer_id=" + peerId +
        "&count=" + std::to_string(count) +
        "&v=5.131&access_token=" + token;

    HttpClient::Request req;
    req.url = url;
    req.timeout_seconds = 10;

    auto resp = m_httpClient.Execute(req);
    if (resp.status_code != 200) return "";

    auto json = ParseJson(resp.body);
    if (json.isMember("error")) return "";

    auto& items = json["response"]["items"];
    if (!items.isArray() || items.empty()) return "";

    std::vector<std::string> fromIds;
    std::set<std::string> seen;
    for (const auto& m : items) {
        std::string fid = std::to_string(GetInt(m, "from_id"));
        if (seen.insert(fid).second) fromIds.push_back(fid);
    }

    auto names = ResolveVKUsers(state, fromIds);
    std::string botName = "Bot";
    if (!state->group_id.empty()) botName = "Bot (" + state->group_id + ")";

    std::string history;
    for (const auto& m : items) {
        std::string fid = std::to_string(GetInt(m, "from_id"));
        std::string name;
        if (fid[0] == '-') {
            name = botName;
        } else {
            name = names.count(fid) ? names[fid] : fid;
        }
        std::string text = GetString(m, "text");
        if (text.empty() && m.isMember("action")) continue;
        if (!history.empty()) history += "\n";
        history += name + " (" + fid + "): " + text;
    }

    return history;
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

void ChannelManager::DispatchToSession(PollerState* state,
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

    m_dispatch(state->agent_id, sessionKey, message, sessionType, replyTarget);
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
//   - A trantor::EventLoop drives the WebSocketClient
//   - loop.loop() blocks until shutdown (state->active = false)
//   - On shutdown, stop the WS client and quit the loop
// ============================================================================

void ChannelManager::EmailWebSocketLoop(PollerState* state) {
    std::cerr << "[email] WebSocket loop starting for " << state->channel_name << std::endl;

    std::string apiKey = GetString(state->config, "api_key");
    std::string inboxId = GetString(state->config, "inbox_id");

    if (apiKey.empty() || inboxId.empty()) {
        std::cerr << "[email] WS: missing api_key or inbox_id — falling back to polling" << std::endl;
        EmailPollLoop(state);
        return;
    }

    // Create a trantor EventLoop for this thread.
    // Drogon's WebSocketClient requires an event loop to process async events.
    trantor::EventLoop loop;

    auto wsPtr = drogon::WebSocketClient::newWebSocketClient(
        "wss://ws.agentmail.to", &loop);

    // --- Message handler: processes inbound AgentMail events ---
    wsPtr->setMessageHandler(
        [this, state](std::string&& message,
                      const drogon::WebSocketClientPtr&,
                      const drogon::WebSocketMessageType& type) {
            if (type != drogon::WebSocketMessageType::Text) return;

            Json::Value root;
            Json::CharReaderBuilder builder;
            std::istringstream stream(message);
            std::string errors;
            if (!Json::parseFromStream(builder, stream, &root, &errors)) {
                std::cerr << "[email] WS: JSON parse error: " << errors << std::endl;
                return;
            }

            std::string eventType = GetString(root, "event_type");
            std::string msgType = GetString(root, "type");

            // Subscription confirmation
            if (msgType == "subscribed") {
                std::cerr << "[email] WS: subscribed to "
                          << GetString(root, "event_types", "") << std::endl;
                return;
            }

            // Error from AgentMail
            if (msgType == "error") {
                std::string errName = GetString(root, "name");
                std::string errMsg = GetString(root, "message");
                std::cerr << "[email] WS error: " << errName
                          << ": " << errMsg << std::endl;

                // Auth errors are fatal — fall back to REST polling
                if (errName == "authentication_error" ||
                    errName == "authorization_error" ||
                    errName == "invalid_api_key") {
                    std::cerr << "[email] WS: fatal auth error — falling back to polling"
                              << std::endl;
                    state->ws_connected = false;
                    state->active = false; // triggers shutdown check
                }
                return;
            }

            // Process message.received events
            if (eventType == "message.received" ||
                eventType == "message.received.spam" ||
                eventType == "message.received.blocked" ||
                eventType == "message.received.unauthenticated") {
                std::string eventId = GetString(root, "event_id");

                // Deduplicate by event_id
                if (!eventId.empty()) {
                    if (state->seenEventStrIds.count(eventId)) return;
                    state->seenEventStrIds.insert(eventId);
                    if (state->seenEventStrIds.size() > 200) {
                        state->seenEventStrIds.erase(state->seenEventStrIds.begin());
                    }
                }

                Json::Value msgObj = root["message"];
                std::string threadId = GetString(msgObj, "thread_id");
                std::string messageId = GetString(msgObj, "message_id");
                std::string sender = GetString(msgObj, "from");
                std::string subject = GetString(msgObj, "subject");
                std::string bodyText = GetString(msgObj, "text");

                // Fallback chain: text → extracted_text → strip html
                if (bodyText.empty()) {
                    bodyText = GetString(msgObj, "extracted_text");
                }
                if (bodyText.empty()) {
                    std::string htmlBody = GetString(msgObj, "html");
                    if (!htmlBody.empty()) {
                        bodyText = StripHtmlSimple(htmlBody);
                    }
                }

                std::cerr << "[email] WS: " << eventType << " from " << sender
                          << " thread=" << threadId << std::endl;

                // Update liveness tracker
                state->last_ws_event = std::chrono::steady_clock::now();

                // Only dispatch normal inbound messages to agent
                if (eventType == "message.received") {
                    ProcessEmailMessage(state, threadId, messageId, sender, subject, bodyText);
                } else {
                    std::cerr << "[email] WS: skipped " << eventType
                              << " (spam/blocked/unauthenticated)" << std::endl;
                }
            }
        });

    // --- Connection closed handler: log and let Drogon auto-reconnect ---
    wsPtr->setConnectionClosedHandler(
        [state](const drogon::WebSocketClientPtr&) {
            std::cerr << "[email] WS: connection closed for "
                      << state->channel_name << std::endl;
            state->ws_connected = false;
            // Drogon's WebSocketClient auto-reconnects (1s delay).
            // We don't quit the loop — just wait for reconnect.
        });

    // --- Build connect request ---
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setPath("/v0");
    req->setParameter("api_key", apiKey);

    std::cerr << "[email] WS: connecting to wss://ws.agentmail.to/v0 for "
              << state->channel_name << std::endl;

    // --- Connect ---
    wsPtr->connectToServer(
        req,
        [this, state, wsPtr, &inboxId](drogon::ReqResult r,
                                        const drogon::HttpResponsePtr&,
                                        const drogon::WebSocketClientPtr&) {
            if (r != drogon::ReqResult::Ok) {
                state->ws_connected = false;
                state->consecutive_errors++;
                std::cerr << "[email] WS: connection failed (attempt "
                          << state->consecutive_errors << ")" << std::endl;

                // After 5 consecutive failures, fall back to REST polling
                if (state->consecutive_errors >= 5) {
                    std::cerr << "[email] WS: too many failures — falling back to polling"
                              << std::endl;
                    wsPtr->stop();
                    // Will exit loop, then caller falls through to polling
                    return;
                }
                // Drogon will auto-reconnect for transient failures
                return;
            }

            std::cerr << "[email] WS: connected for " << state->channel_name << std::endl;
            state->ws_connected = true;
            state->consecutive_errors = 0;

            // Subscribe to message.received events for our inbox
            Json::Value subscribe;
            subscribe["type"] = "subscribe";
            subscribe["event_types"] = Json::Value(Json::arrayValue);
            subscribe["event_types"].append("message.received");
            subscribe["inbox_ids"] = Json::Value(Json::arrayValue);
            subscribe["inbox_ids"].append(inboxId);

            Json::StreamWriterBuilder wb;
            wb.settings_["indentation"] = "";
            std::string subMsg = Json::writeString(wb, subscribe);
            wsPtr->getConnection()->send(subMsg);

            std::cerr << "[email] WS: subscribed for inbox " << inboxId << std::endl;

            // Keepalive ping every 30s
            wsPtr->getConnection()->setPingMessage("", std::chrono::seconds(30));
        });

    // Initialize liveness tracker
    state->last_ws_event = std::chrono::steady_clock::now();

    // Schedule periodic checks: shutdown + liveness monitoring
    loop.runEvery(5.0, [this, state, &loop, wsPtr]() {
        if (!state->active) {
            std::cerr << "[email] WS: shutting down" << std::endl;
            wsPtr->stop();
            loop.quit();
            return;
        }

        // Liveness check: if connected but no events for 5 minutes,
        // the connection may be stale. Increment error counter.
        if (state->ws_connected) {
            auto elapsed = std::chrono::steady_clock::now() - state->last_ws_event;
            auto minutes = std::chrono::duration_cast<std::chrono::minutes>(elapsed);
            if (minutes.count() >= 5) {
                state->consecutive_errors++;
                std::cerr << "[email] WS: no events for " << minutes.count()
                          << " minutes (stale connection, attempt "
                          << state->consecutive_errors << ")" << std::endl;
                state->last_ws_event = std::chrono::steady_clock::now();

                if (state->consecutive_errors >= 3) {
                    std::cerr << "[email] WS: stale connection — reconnecting" << std::endl;
                    state->ws_connected = false;
                    wsPtr->stop();
                    // Reconnect: the loop stays running, Drogon will reconnect
                    state->consecutive_errors = 0;
                }
            }
        }
    });

    // Run the event loop — blocks until quit() is called from shutdown check
    loop.loop();

    std::cerr << "[email] WebSocket loop stopped for " << state->channel_name << std::endl;

    // If we stopped due to persistent failures (not clean shutdown), fall back to polling
    if (state->active && state->consecutive_errors >= 5) {
        std::cerr << "[email] Falling back to REST polling for "
                  << state->channel_name << std::endl;
        state->ws_connected = false;
        state->consecutive_errors = 0; // reset for poll loop
        EmailPollLoop(state);
    }
}

// ============================================================================
// Email Polling Loop (REST-based fallback)
//
// Used when WebSocket connection is unavailable.
// Polls GET /v0/inboxes/{inbox_id}/messages every ~25 seconds.
// ============================================================================

void ChannelManager::EmailPollLoop(PollerState* state) {
    std::cerr << "[email] Poll loop started for " << state->channel_name << std::endl;

    while (state->active) {
        // Wait with backoff
        {
            auto now = std::chrono::steady_clock::now();
            if (now < state->next_attempt) {
                std::this_thread::sleep_for(state->next_attempt - now);
            }
        }
        if (!state->active) break;

        std::string apiKey = GetString(state->config, "api_key");
        std::string inboxId = GetString(state->config, "inbox_id");
        int waitSec = 25; // default polling interval

        if (apiKey.empty() || inboxId.empty()) {
            std::cerr << "[email] Missing api_key or inbox_id, retrying in 60s" << std::endl;
            state->consecutive_errors++;
            state->next_attempt = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            continue;
        }

        try {
            // Poll recent messages from AgentMail
            std::string url = "https://api.agentmail.to/v0/inboxes/" + inboxId + "/messages?limit=10";
            
            // Use last seen timestamp to only get new messages
            if (!state->lp_ts.empty()) {
                url += "&after=" + UrlEncode(state->lp_ts);
            }

            HttpClient::Request req;
            req.method = "GET";
            req.url = url;
            req.headers["Authorization"] = "Bearer " + apiKey;
            req.headers["Content-Type"] = "application/json";

            auto resp = m_httpClient.Execute(req);

            if (resp.status_code != 200) {
                std::cerr << "[email] Poll failed: " << resp.status_code
                          << " body=" << resp.body.substr(0, 200) << std::endl;
                state->consecutive_errors++;
                int backoff = std::min(300, 5 * state->consecutive_errors);
                state->next_attempt = std::chrono::steady_clock::now() + std::chrono::seconds(backoff);
                continue;
            }

            // Parse response
            Json::CharReaderBuilder builder;
            Json::Value root;
            std::istringstream stream(resp.body);
            std::string errors;
            if (!Json::parseFromStream(builder, stream, &root, &errors)) {
                std::cerr << "[email] Parse error: " << errors << std::endl;
                state->consecutive_errors++;
                state->next_attempt = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                continue;
            }

            // Reset error state on success
            state->consecutive_errors = 0;

            // Process messages
            Json::Value messages = root["messages"];
            if (messages.isArray()) {
                std::string latestTimestamp;
                for (const auto& msg : messages) {
                    std::string msgId = GetString(msg, "id");
                    std::string threadId = GetString(msg, "thread_id");
                    std::string sender = GetString(msg, "from");
                    std::string subject = GetString(msg, "subject");
                    std::string createdAt = GetString(msg, "created_at");
                    std::string bodyText = GetString(msg, "text");

                    // Skip if already seen (deduplicate by message ID)
                    int64_t msgHash = 0;
                    for (char c : msgId) msgHash = msgHash * 31 + c;
                    if (state->seenEventIds.count(msgHash)) continue;
                    state->seenEventIds.insert(msgHash);
                    if (state->seenEventIds.size() > PollerState::kMaxSeenIds) {
                        state->seenEventIds.erase(state->seenEventIds.begin());
                    }

                    // Track latest timestamp
                    if (createdAt > latestTimestamp) latestTimestamp = createdAt;

                    // Determine if this is inbound (from external sender, not from us)
                    // AgentMail marks direction: "inbound" for received
                    std::string direction = GetString(msg, "direction");
                    if (direction != "inbound") continue;

                    ProcessEmailMessage(state, threadId, msgId, sender, subject, bodyText);
                }

                // Persist the latest timestamp for next poll
                if (!latestTimestamp.empty()) {
                    state->lp_ts = latestTimestamp;
                    if (m_configStore) {
                        m_configStore->Set(state->agent_id,
                            "channel." + state->channel_name + ".polling.last_before",
                            latestTimestamp);
                    }
                }
            }

            state->next_attempt = std::chrono::steady_clock::now() + std::chrono::seconds(waitSec);

        } catch (const std::exception& e) {
            std::cerr << "[email] Exception in poll loop: " << e.what() << std::endl;
            state->consecutive_errors++;
            int backoff = std::min(300, 5 * state->consecutive_errors);
            state->next_attempt = std::chrono::steady_clock::now() + std::chrono::seconds(backoff);
        }
    }

    std::cerr << "[email] Poll loop stopped for " << state->channel_name << std::endl;
}

void ChannelManager::ProcessEmailMessage(PollerState* state,
                                          const std::string& threadId,
                                          const std::string& messageId,
                                          const std::string& sender,
                                          const std::string& subject,
                                          const std::string& bodyText) {
    std::string routingKey = "thread:" + threadId;
    std::string sessionType = "email:chat";

    // Build prompt — include message_id and thread_id so the agent can reply directly
    // without searching for the email first
    std::string prompt;
    if (!subject.empty()) {
        prompt = "New email from " + sender + " with subject '" + subject + "':\n\n" + bodyText;
    } else {
        prompt = "New email from " + sender + ":\n\n" + bodyText;
    }

    prompt += "\n\nMessage-ID: " + messageId;
    prompt += "\nThread-ID: " + threadId;
    prompt += "\n\nYou are responding via email. Use the email tool with action=reply and the message_id above to respond. Do NOT use the social tool to reply.";

    DispatchToSession(state, routingKey, prompt, sessionType);

    std::cerr << "[email] Dispatched message " << messageId
              << " from " << sender << " thread=" << threadId << std::endl;
}

std::string ChannelManager::GetWhatsAppQrUrl(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_pollersMutex);
    auto it = m_pollers.find(name);
    if (it == m_pollers.end()) return "";
    auto& state = it->second;
    if (state->channel_type != "whatsapp") return "";
    std::lock_guard<std::mutex> qrLock(state->whatsapp_qr_mutex);
    return state->whatsapp_qr_url;
}

// ============================================================================
// Slack Polling Loop (Phase 1 — REST polling)
// ============================================================================
// Slack Socket Mode Loop (Phase 2 — WebSocket real-time)
//
// Protocol:
//   1. POST apps.connections.open with app-level token → get wss:// URL
//   2. Connect WebSocket to that URL
//   3. Receive "hello" → connected
//   4. Receive event envelopes: {type, envelope_id, payload}
//   5. Ack each event by sending back {"envelope_id": "..."}
//   6. On "disconnect" → reconnect via apps.connections.open
//
// No heartbeats, no sequence numbers, no resume.
// Much simpler than Discord Gateway.
// ============================================================================

void ChannelManager::SlackSocketModeLoop(PollerState* state) {
    std::cerr << "[slack-socket] Socket Mode loop starting for " << state->channel_name << std::endl;

    const std::string appToken = GetString(state->config, "app_token");
    const std::string botToken = GetString(state->config, "bot_token");

    if (appToken.empty()) {
        std::cerr << "[slack-socket] No app_token — falling back to polling" << std::endl;
        SlackPollingLoop(state);
        return;
    }

    // Resolve bot user ID for self-filtering
    std::string botUserId = GetString(state->config, "bot_user_id");
    if (botUserId.empty() && !botToken.empty()) {
        HttpClient::Request authReq;
        authReq.method = "POST";
        authReq.url = "https://slack.com/api/auth.test";
        authReq.headers["Authorization"] = "Bearer " + botToken;
        authReq.headers["Content-Type"] = "application/x-www-form-urlencoded";
        authReq.body = "";
        authReq.follow_redirects = false;

        auto authResp = m_httpClient.Execute(authReq);
        if (authResp.status_code == 200) {
            Json::Value data;
            Json::CharReaderBuilder crb;
            std::istringstream iss(authResp.body);
            std::string errs;
            if (Json::parseFromStream(crb, iss, &data, &errs) && data.isMember("ok") && data["ok"].asBool()) {
                botUserId = data["user_id"].asString();
                std::cerr << "[slack-socket] Resolved bot user ID: " << botUserId << std::endl;
            }
        }
    }

    while (state->active) {
        // --- Step 1: Get WebSocket URL from apps.connections.open ---
        HttpClient::Request connReq;
        connReq.method = "POST";
        connReq.url = "https://slack.com/api/apps.connections.open";
        connReq.headers["Authorization"] = "Bearer " + appToken;
        connReq.headers["Content-Type"] = "application/x-www-form-urlencoded";
        connReq.body = "";
        connReq.follow_redirects = false;

        std::cerr << "[slack-socket] Requesting WebSocket URL..." << std::endl;
        auto connResp = m_httpClient.Execute(connReq);

        if (connResp.status_code != 200) {
            std::cerr << "[slack-socket] apps.connections.open failed (HTTP "
                      << connResp.status_code << "): " << connResp.body.substr(0, 300) << std::endl;
            // Back off and retry
            for (int i = 0; i < 30 && state->active; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            continue;
        }

        Json::Value connData;
        Json::CharReaderBuilder crb;
        std::istringstream connIss(connResp.body);
        std::string connErrs;
        if (!Json::parseFromStream(crb, connIss, &connData, &connErrs) ||
            !connData.isMember("ok") || !connData["ok"].asBool() ||
            !connData.isMember("url")) {
            std::cerr << "[slack-socket] Bad response from apps.connections.open: "
                      << connResp.body.substr(0, 300) << std::endl;
            for (int i = 0; i < 30 && state->active; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            continue;
        }

        std::string wsUrl = connData["url"].asString();
        std::cerr << "[slack-socket] Got WebSocket URL: " << wsUrl.substr(0, 60) << "..." << std::endl;

        // --- Step 2: Connect to WebSocket ---
        trantor::EventLoop loop;

        // Parse the wss:// URL into host + path for Drogon
        // wsUrl = "wss://wss.slack.com/link/?ticket=..."
        std::string wsHost, wsPath;
        {
            // Extract host and path from wss:// URL
            std::string url = wsUrl;
            // Remove "wss://" prefix
            size_t hostStart = url.find("://");
            if (hostStart != std::string::npos) {
                hostStart += 3;
                size_t pathStart = url.find("/", hostStart);
                if (pathStart != std::string::npos) {
                    wsHost = url.substr(hostStart, pathStart - hostStart);
                    wsPath = url.substr(pathStart);
                } else {
                    wsHost = url.substr(hostStart);
                    wsPath = "/";
                }
            }
        }

        // Prepend wss:// for Drogon's newWebSocketClient
        std::string wsConnectUrl = "wss://" + wsHost;

        auto wsPtr = drogon::WebSocketClient::newWebSocketClient(wsConnectUrl, &loop);

        // --- Message handler ---
        wsPtr->setMessageHandler(
            [this, state, &botUserId, wsPtr](std::string&& message,
                        const drogon::WebSocketClientPtr&,
                        const drogon::WebSocketMessageType& type) {
                // Drogon types: Text=0, Binary=1, Ping=2, Pong=3, Close=4
                if (type == drogon::WebSocketMessageType::Ping ||
                    type == drogon::WebSocketMessageType::Pong) {
                    // Control frames — skip
                    return;
                }

                // Log all non-control frames
                std::cerr << "[slack-socket] Frame: type=" << static_cast<int>(type)
                          << " len=" << message.size()
                          << " content=" << message.substr(0, 200) << std::endl;

                if (type != drogon::WebSocketMessageType::Text &&
                    type != drogon::WebSocketMessageType::Binary) {
                    return;
                }

                Json::Value envelope;
                Json::CharReaderBuilder builder;
                std::istringstream stream(message);
                std::string errors;
                if (!Json::parseFromStream(builder, stream, &envelope, &errors)) {
                    std::cerr << "[slack-socket] JSON parse error: " << errors << std::endl;
                    return;
                }

                std::string msgType = GetString(envelope, "type");
                std::string envelopeId = GetString(envelope, "envelope_id");

                // Log every envelope for debugging
                std::cerr << "[slack-socket] Received envelope: type=" << msgType;
                if (!envelopeId.empty()) std::cerr << " envelope_id=" << envelopeId;
                std::cerr << std::endl;

                // --- hello: connection established ---
                if (msgType == "hello") {
                    std::cerr << "[slack-socket] Connected (hello received)" << std::endl;
                    state->ws_connected = true;
                    state->consecutive_errors = 0;
                    return;
                }

                // --- disconnect: need to reconnect ---
                if (msgType == "disconnect") {
                    std::string reason = GetString(envelope, "reason");
                    std::cerr << "[slack-socket] Disconnect received: " << reason << std::endl;
                    state->ws_connected = false;

                    // Stop the WS client — outer loop will reconnect
                    wsPtr->stop();
                    return;
                }

                // --- Acknowledge all envelopes ---
                if (!envelopeId.empty()) {
                    Json::Value ack;
                    ack["envelope_id"] = envelopeId;
                    Json::StreamWriterBuilder wb;
                    wb.settings_["indentation"] = "";
                    std::string ackMsg = Json::writeString(wb, ack);
                    auto conn = wsPtr->getConnection();
                    if (conn) {
                        conn->send(ackMsg);
                    }
                }

                // --- Process events_api envelopes ---
                if (msgType == "events_api") {
                    Json::Value payload = envelope["payload"];
                    Json::Value event = payload["event"];

                    if (!event.isObject()) {
                        std::cerr << "[slack-socket] events_api with no event object, skipping" << std::endl;
                        return;
                    }

                    std::string eventType = GetString(event, "type");
                    std::cerr << "[slack-socket] Event type: " << eventType << std::endl;

                    // Only handle message events
                    if (eventType != "message") return;

                    // Skip subtype messages (joins, leaves, channel_topic, etc.)
                    if (event.isMember("subtype")) {
                        std::cerr << "[slack-socket] Skipping subtype: " << GetString(event, "subtype") << std::endl;
                        return;
                    }

                    // Skip bot messages
                    std::string botId = GetString(event, "bot_id");
                    if (!botId.empty()) {
                        std::cerr << "[slack-socket] Skipping bot message: " << botId << std::endl;
                        return;
                    }

                    // Self-filter
                    std::string userId = GetString(event, "user");
                    if (userId == botUserId) {
                        std::cerr << "[slack-socket] Skipping self (userId=" << userId << ")" << std::endl;
                        return;
                    }

                    std::string text = GetString(event, "text");
                    std::string channel = GetString(event, "channel");
                    std::string ts = GetString(event, "ts");
                    std::string threadTs = GetString(event, "thread_ts");

                    if (text.empty() || channel.empty() || ts.empty()) {
                        std::cerr << "[slack-socket] Skipping: missing text/channel/ts" << std::endl;
                        return;
                    }

                    // Check respond_to_mentions / respond_to_all
                    bool respondToAll = GetString(state->config, "respond_to_all_messages") == "true";
                    bool respondToMentions = GetString(state->config, "respond_to_mentions") != "false";
                    bool isMention = !botUserId.empty() &&
                        text.find("<@" + botUserId + ">") != std::string::npos;

                    std::cerr << "[slack-socket] Message from " << userId << " in " << channel
                              << " respondToAll=" << respondToAll
                              << " respondToMentions=" << respondToMentions
                              << " isMention=" << isMention << std::endl;

                    if (!respondToAll && !isMention && respondToMentions) {
                        std::cerr << "[slack-socket] Filtered: not mention, not respond-to-all" << std::endl;
                        return;
                    }

                    // Strip mention from text
                    std::string cleanText = text;
                    if (!botUserId.empty()) {
                        std::string mentionTag = "<@" + botUserId + ">";
                        size_t pos;
                        while ((pos = cleanText.find(mentionTag)) != std::string::npos) {
                            cleanText.erase(pos, mentionTag.size());
                        }
                        // Trim
                        while (!cleanText.empty() && (cleanText[0] == ' ' || cleanText[0] == '\n')) {
                            cleanText.erase(0, 1);
                        }
                        while (!cleanText.empty() && (cleanText.back() == ' ' || cleanText.back() == '\n')) {
                            cleanText.pop_back();
                        }
                    }

                    // Build routing key
                    // threaded_replies: each top-level channel message starts its own thread session
                    // non-threaded: all channel messages share one session per channel
                    bool threaded = (GetString(state->config, "threaded_replies") == "true");
                    std::string routingKey = "chat:slack:" + channel;
                    if (!threadTs.empty() && threadTs != ts) {
                        // Already in a thread — always route to thread
                        routingKey = "chat:slack:" + threadTs;
                    } else if (threaded && threadTs.empty()) {
                        // Top-level message + threaded mode: use message ts as routing key
                        // This creates a per-message session and replies thread under it
                        routingKey = "chat:slack:" + ts;
                    }

                    // Build prompt
                    std::string prompt = "[Slack message from <" + userId + ">";
                    prompt += " in channel " + channel;
                    if (!threadTs.empty() && threadTs != ts) {
                        prompt += " (thread " + threadTs + ")";
                    }
                    prompt += "]\n" + cleanText;

                    std::string sessionType = "slack";

                    // Build reply target
                    ReplyTarget replyTarget;
                    replyTarget.channel_name = state->channel_name;
                    replyTarget.channel_type = state->channel_type;
                    replyTarget.type = ReplyTarget::Chat;
                    replyTarget.peer_id = channel;
                    if (!threadTs.empty() && threadTs != ts) {
                        // Message is in an existing thread
                        replyTarget.reply_to_comment = threadTs;
                    } else if (threaded && threadTs.empty()) {
                        // Top-level message in threaded mode: reply in thread under this message
                        replyTarget.reply_to_comment = ts;
                    }

                    m_dispatch(state->agent_id, routingKey, prompt, sessionType, replyTarget);

                    std::cerr << "[slack-socket] Dispatched message from " << userId
                              << " in " << channel << " ts=" << ts << std::endl;
                }
            });

        // --- Connection closed handler ---
        wsPtr->setConnectionClosedHandler(
            [state](const drogon::WebSocketClientPtr&) {
                std::cerr << "[slack-socket] WebSocket closed for "
                          << state->channel_name << std::endl;
                state->ws_connected = false;
            });

        // --- Build connect request ---
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setPath(wsPath);

        std::cerr << "[slack-socket] Connecting to " << wsHost << wsPath.substr(0, 30) << "..." << std::endl;

        // --- Connect ---
        wsPtr->connectToServer(
            req,
            [state, wsPtr](drogon::ReqResult r,
                           const drogon::HttpResponsePtr& resp,
                           const drogon::WebSocketClientPtr&) {
                if (r != drogon::ReqResult::Ok) {
                    state->ws_connected = false;
                    state->consecutive_errors++;
                    std::cerr << "[slack-socket] Connection failed (attempt "
                              << state->consecutive_errors << ")" << std::endl;
                    return;
                }

                std::cerr << "[slack-socket] WebSocket connected, waiting for hello..." << std::endl;
            });

        // --- Run the event loop with periodic shutdown check ---
        // Check every 5s if we should shut down
        loop.runEvery(5.0, [state, &loop, wsPtr]() {
            if (!state->active) {
                std::cerr << "[slack-socket] Shutting down" << std::endl;
                wsPtr->stop();
                loop.quit();
            }
        });

        // Block here until WS disconnects or shutdown
        loop.loop();

        state->ws_connected = false;

        if (!state->active) break;  // Clean shutdown

        // Connection dropped — back off briefly and reconnect
        std::cerr << "[slack-socket] Reconnecting in 5s..." << std::endl;
        for (int i = 0; i < 5 && state->active; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    std::cerr << "[slack-socket] Socket Mode loop stopped for " << state->channel_name << std::endl;
}

// ============================================================================
// Slack REST Polling Loop (Phase 1 — fallback)
// ============================================================================

void ChannelManager::SlackPollingLoop(PollerState* state) {
    std::cerr << "[slack] Polling loop started for " << state->channel_name << std::endl;

    const std::string botToken = GetString(state->config, "bot_token");
    if (botToken.empty()) {
        std::cerr << "[slack] No bot_token configured for " << state->channel_name << std::endl;
        return;
    }

    // Resolve bot user ID for self-filtering
    std::string botUserId = GetString(state->config, "bot_user_id");
    if (botUserId.empty()) {
        // Fetch via auth.test
        HttpClient::Request req;
        req.method = "POST";
        req.url = "https://slack.com/api/auth.test";
        req.headers["Authorization"] = "Bearer " + botToken;
        req.headers["Content-Type"] = "application/x-www-form-urlencoded";
        req.body = "";
        req.follow_redirects = false;

        auto resp = m_httpClient.Execute(req);
        if (resp.status_code == 200) {
            Json::Value data;
            Json::CharReaderBuilder crb;
            std::istringstream iss(resp.body);
            std::string errs;
            if (Json::parseFromStream(crb, iss, &data, &errs) && data.isMember("ok") && data["ok"].asBool()) {
                botUserId = data["user_id"].asString();
                std::cerr << "[slack] Resolved bot user ID: " << botUserId << std::endl;
            }
        }
    }

    // Discover channels the bot is a member of via conversations.list
    // If monitored_channels is explicitly set, use that instead (manual override).
    // Otherwise, auto-discover all channels the bot has joined.
    std::vector<std::string> monitoredChannels;
    std::string channelsJson = GetString(state->config, "monitored_channels");
    if (!channelsJson.empty()) {
        // Manual override: parse JSON array of channel IDs
        Json::Value arr;
        Json::CharReaderBuilder crb;
        std::istringstream iss(channelsJson);
        std::string errs;
        if (Json::parseFromStream(crb, iss, &arr, &errs) && arr.isArray()) {
            for (Json::ArrayIndex i = 0; i < arr.size(); ++i) {
                monitoredChannels.push_back(arr[i].asString());
            }
        }
        std::cerr << "[slack] Using " << monitoredChannels.size() << " manually configured channels" << std::endl;
    } else {
        // Auto-discover: fetch all channels the bot is a member of
        HttpClient::Request req;
        req.method = "GET";
        req.url = "https://slack.com/api/conversations.list?types=public_channel,private_channel&limit=1000";
        req.headers["Authorization"] = "Bearer " + botToken;
        req.follow_redirects = false;

        auto resp = m_httpClient.Execute(req);
        if (resp.status_code == 200) {
            Json::Value data;
            Json::CharReaderBuilder crb;
            std::istringstream iss(resp.body);
            std::string errs;
            if (Json::parseFromStream(crb, iss, &data, &errs) && data.isMember("ok") && data["ok"].asBool()) {
                if (data.isMember("channels") && data["channels"].isArray()) {
                    for (const auto& ch : data["channels"]) {
                        if (ch.isMember("is_member") && ch["is_member"].asBool()) {
                            std::string id = ch["id"].asString();
                            std::string name = ch.isMember("name") ? ch["name"].asString() : id;
                            monitoredChannels.push_back(id);
                            std::cerr << "[slack] Auto-discovered channel: #" << name << " (" << id << ")" << std::endl;
                        }
                    }
                }
            }
        }
        std::cerr << "[slack] Auto-discovered " << monitoredChannels.size() << " channels the bot is a member of" << std::endl;
    }

    if (monitoredChannels.empty()) {
        std::cerr << "[slack] No channels found (bot not in any channels, and no manual override). "
                  << "Polling loop idle for " << state->channel_name << std::endl;
        // Stay alive but don't poll — config may be updated later
        while (state->active) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
        }
        return;
    }

    // Track latest timestamp per channel (watermark)
    // lp_ts holds the initial value from config store; we update it as we go.
    std::string latestTs = state->lp_ts;  // "seconds.microseconds" format

    const auto pollInterval = std::chrono::seconds(10);  // Tier 2: ~20/min, we use 6/min

    while (state->active) {
        state->next_attempt = std::chrono::steady_clock::now() + pollInterval;

        for (const auto& channelId : monitoredChannels) {
            if (!state->active) break;

            try {
                std::string url = "https://slack.com/api/conversations.history?channel="
                    + channelId + "&limit=5";
                if (!latestTs.empty()) {
                    url += "&oldest=" + latestTs;
                }

                HttpClient::Request req;
                req.method = "GET";
                req.url = url;
                req.headers["Authorization"] = "Bearer " + botToken;
                req.follow_redirects = false;

                auto resp = m_httpClient.Execute(req);

                if (resp.status_code == 429) {
                    // Rate limited — back off
                    std::string retryAfter = "10";
                    if (resp.headers.count("retry-after")) {
                        retryAfter = resp.headers.at("retry-after");
                    }
                    int waitSec = std::stoi(retryAfter);
                    std::cerr << "[slack] Rate limited, waiting " << waitSec << "s" << std::endl;
                    std::this_thread::sleep_for(std::chrono::seconds(waitSec));
                    continue;
                }

                if (resp.status_code != 200) {
                    std::cerr << "[slack] conversations.history failed ("
                              << resp.status_code << "): " << resp.body.substr(0, 200) << std::endl;
                    continue;
                }

                Json::Value data;
                Json::CharReaderBuilder crb;
                std::istringstream iss(resp.body);
                std::string errs;
                if (!Json::parseFromStream(crb, iss, &data, &errs) || !data.isMember("ok") || !data["ok"].asBool()) {
                    std::cerr << "[slack] API error: " << resp.body.substr(0, 200) << std::endl;
                    continue;
                }

                if (!data.isMember("messages") || !data["messages"].isArray()) continue;

                // Process messages in chronological order (API returns newest-first)
                const auto& messages = data["messages"];
                for (int i = static_cast<int>(messages.size()) - 1; i >= 0; --i) {
                    const auto& msg = messages[i];
                    if (!msg.isObject()) continue;

                    // Self-filter: ignore bot's own messages
                    std::string userId = msg.isMember("user") ? msg["user"].asString() : "";
                    if (userId == botUserId) continue;

                    // Skip bot messages
                    std::string botId = msg.isMember("bot_id") ? msg["bot_id"].asString() : "";
                    if (!botId.empty()) continue;

                    // Skip subtype messages (joins, leaves, etc.)
                    if (msg.isMember("subtype")) continue;

                    std::string text = msg.isMember("text") ? msg["text"].asString() : "";
                    std::string ts = msg.isMember("ts") ? msg["ts"].asString() : "";
                    std::string threadTs = msg.isMember("thread_ts") ? msg["thread_ts"].asString() : "";

                    if (text.empty() || ts.empty()) continue;

                    // Check respond_to_mentions / respond_to_all
                    bool respondToAll = GetString(state->config, "respond_to_all_messages") == "true";
                    bool respondToMentions = GetString(state->config, "respond_to_mentions") != "false";

                    bool isMention = text.find("<@" + botUserId + ">") != std::string::npos;

                    if (!respondToAll && !isMention && respondToMentions) continue;
                    if (!respondToAll && !respondToMentions) continue;

                    // Strip the mention from the text for cleaner dispatch
                    std::string cleanText = text;
                    std::string mentionTag = "<@" + botUserId + ">";
                    size_t pos;
                    while ((pos = cleanText.find(mentionTag)) != std::string::npos) {
                        cleanText.erase(pos, mentionTag.size());
                    }
                    // Trim leading whitespace
                    while (!cleanText.empty() && (cleanText[0] == ' ' || cleanText[0] == '\n')) {
                        cleanText.erase(0, 1);
                    }
                    while (!cleanText.empty() && (cleanText.back() == ' ' || cleanText.back() == '\n')) {
                        cleanText.pop_back();
                    }

                    // Dispatch to session
                    bool threaded = (GetString(state->config, "threaded_replies") == "true");
                    std::string routingKey = "chat:slack:" + channelId;
                    if (!threadTs.empty() && threadTs != ts) {
                        // Already in a thread — always route to thread
                        routingKey = "chat:slack:" + threadTs;
                    } else if (threaded && threadTs.empty()) {
                        // Top-level message + threaded mode: per-message session
                        routingKey = "chat:slack:" + ts;
                    }

                    // Build message prompt with context
                    std::string prompt = "[Slack message from <" + userId + ">";
                    prompt += " in channel " + channelId;
                    if (!threadTs.empty() && threadTs != ts) {
                        prompt += " (thread " + threadTs + ")";
                    }
                    prompt += "]\n" + cleanText;

                    std::string sessionType = "slack";

                    // Build reply target for auto-reply routing
                    ReplyTarget replyTarget;
                    replyTarget.channel_name = state->channel_name;
                    replyTarget.channel_type = state->channel_type;
                    replyTarget.type = ReplyTarget::Chat;
                    replyTarget.peer_id = channelId;
                    if (!threadTs.empty() && threadTs != ts) {
                        // Message is in an existing thread
                        replyTarget.reply_to_comment = threadTs;
                    } else if (threaded && threadTs.empty()) {
                        // Top-level message in threaded mode: reply in thread under this message
                        replyTarget.reply_to_comment = ts;
                    }

                    m_dispatch(state->agent_id, routingKey, prompt, sessionType, replyTarget);

                    std::cerr << "[slack] Dispatched message from " << userId
                              << " in " << channelId << " ts=" << ts << std::endl;

                    // Update watermark
                    latestTs = ts;
                }
            } catch (const std::exception& e) {
                std::cerr << "[slack] Exception in polling loop: " << e.what() << std::endl;
            }
        }

        // Persist watermark
        if (m_configStore && !latestTs.empty()) {
            m_configStore->Set("", "channel." + state->channel_name + ".polling.latest_ts", latestTs);
        }

        // Sleep until next poll
        while (state->active && std::chrono::steady_clock::now() < state->next_attempt) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    std::cerr << "[slack] Polling loop stopped for " << state->channel_name << std::endl;
}

// ============================================================================
// Nextcloud Talk Polling Loop (OCS API — Phase 1a)
//
// Authenticates as a Nextcloud user via HTTP Basic (app password).
// Periodically syncs the conversation list, then long-polls each watched
// conversation for new messages using lookIntoFuture=1.
//
// Config:
//   server_url   — e.g. https://cloud.example.com
//   username     — Nextcloud user (e.g. animus-bot)
//   app_password — app password generated in Nextcloud security settings
//   watch_tokens — optional JSON array of conversation tokens to monitor
//                   (auto-discovers all if empty)
//   respond_in_dm          — respond to all DM messages (default true)
//   respond_in_group_on_mention — only respond to @mentions in groups (default true)
//   group_mention_trigger  — trigger string (default "@animus")
//
// State persistence:
//   Per-conversation lastKnownMessageId stored as
//   channel.{name}.polling.{token}.last_msg_id
// ============================================================================

void ChannelManager::NextcloudTalkPollLoop(PollerState* state) {
    std::cerr << "[nextcloud] Talk poll loop starting for "
              << state->channel_name << std::endl;

    std::string serverUrl = GetString(state->config, "server_url");
    std::string username = GetString(state->config, "username");
    std::string appPassword = GetString(state->config, "app_password");

    if (serverUrl.empty() || username.empty() || appPassword.empty()) {
        std::cerr << "[nextcloud] Missing server_url/username/app_password for "
                  << state->channel_name << std::endl;
        state->active = false;
        return;
    }

    // Trim trailing slash from server_url
    while (!serverUrl.empty() && serverUrl.back() == '/') serverUrl.pop_back();

    std::string authHeader = "Basic " + Base64EncodeStr(username + ":" + appPassword);
    std::string apiBase = serverUrl + "/ocs/v2.php/apps/spreed/api/v4";
    std::string chatApiBase = serverUrl + "/ocs/v2.php/apps/spreed/api/v1";

    bool respondInDm = GetString(state->config, "respond_in_dm", "true") != "false";
    bool respondInGroupOnMention = GetString(state->config, "respond_in_group_on_mention", "true") != "false";
    std::string mentionTrigger = GetString(state->config, "group_mention_trigger", "@" + username);

    // Parse explicit watch_tokens if provided
    std::vector<std::string> watchTokens;
    std::string watchTokensJson = GetString(state->config, "watch_tokens");
    if (!watchTokensJson.empty()) {
        Json::Value arr;
        Json::CharReaderBuilder crb;
        std::istringstream iss(watchTokensJson);
        std::string errs;
        if (Json::parseFromStream(crb, iss, &arr, &errs) && arr.isArray()) {
            for (Json::ArrayIndex i = 0; i < arr.size(); ++i) {
                if (arr[i].isString()) watchTokens.push_back(arr[i].asString());
            }
        }
    }

    // Conversation type constants (Nextcloud Talk)
    // 1=one-to-one, 2=group, 3=public, 4=changelog, 6=note-to-self
    auto isDm = [](int convType) { return convType == 1 || convType == 6; };

    // Per-conversation state: token → {lastMsgId, type, displayName}
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

        auto resp = m_httpClient.Execute(req);
        if (resp.status_code != 200) {
            std::cerr << "[nextcloud] /room failed (" << resp.status_code
                      << "): " << resp.body.substr(0, 200) << std::endl;
            return;
        }

        Json::Value data;
        Json::CharReaderBuilder crb;
        std::istringstream iss(resp.body);
        std::string errs;
        if (!Json::parseFromStream(crb, iss, &data, &errs)) return;

        if (!data.isMember("ocs") || !data["ocs"]["data"].isArray()) return;

        for (const auto& room : data["ocs"]["data"]) {
            std::string token = room.isMember("token") ? room["token"].asString() : "";
            if (token.empty()) continue;

            int convType = room.isMember("type") ? room["type"].asInt() : 2;

            // Skip changelog conversations
            if (convType == 4) continue;

            // If watch_tokens is specified, only include listed tokens
            if (!watchTokens.empty()) {
                bool found = false;
                for (const auto& wt : watchTokens) {
                    if (wt == token) { found = true; break; }
                }
                if (!found) continue;
            }

            std::string displayName = room.isMember("displayName") ? room["displayName"].asString() : token;

            if (conversations.find(token) == conversations.end()) {
                // New conversation — load persisted lastKnownMessageId
                ConvState cs;
                cs.type = convType;
                cs.displayName = displayName;
                if (m_configStore) {
                    std::string stored = m_configStore->Get("",
                        "channel." + state->channel_name + ".polling." + token + ".last_msg_id");
                    if (!stored.empty()) {
                        try { cs.lastMsgId = std::stoi(stored); } catch (...) {}
                    }
                }
                conversations[token] = cs;
                std::cerr << "[nextcloud] Watching conversation: " << displayName
                          << " (token=" << token << ", type=" << convType << ")" << std::endl;
            } else {
                conversations[token].type = convType;
                conversations[token].displayName = displayName;
            }
        }
    };

    // Initial sync
    syncConversationList();

    if (conversations.empty()) {
        std::cerr << "[nextcloud] No conversations found for "
                  << state->channel_name
                  << ". Polling loop idle — will retry conversation sync periodically."
                  << std::endl;
    }

    auto lastRoomSync = std::chrono::steady_clock::now();
    const auto roomSyncInterval = std::chrono::seconds(60);
    const auto longPollTimeout = std::chrono::seconds(30);

    while (state->active && !m_stopRequested) {
        try {
            auto now = std::chrono::steady_clock::now();

            // Periodically refresh conversation list
            if (now - lastRoomSync > roomSyncInterval) {
                syncConversationList();
                lastRoomSync = now;
            }

            if (conversations.empty()) {
                std::this_thread::sleep_for(std::chrono::seconds(10));
                continue;
            }

            // Long-poll each conversation
            for (auto& [token, conv] : conversations) {
                if (!state->active) break;

                std::string url = chatApiBase + "/chat/" + token;
                url += "?lookIntoFuture=1&timeout=30&format=json";
                if (conv.lastMsgId > 0) {
                    url += "&lastKnownMessageId=" + std::to_string(conv.lastMsgId);
                }

                HttpClient::Request req;
                req.method = "GET";
                req.url = url;
                req.headers["Accept"] = "application/json";
                req.headers["OCS-APIREQUEST"] = "true";
                req.headers["Authorization"] = authHeader;
                req.follow_redirects = false;
                req.timeout_seconds = 35; // 30s long-poll + 5s buffer

                auto resp = m_httpClient.Execute(req);

                if (resp.status_code == 304) {
                    // No new messages — expected for long-poll
                    continue;
                }

                if (resp.status_code != 200) {
                    std::cerr << "[nextcloud] /chat/" << token << " failed ("
                              << resp.status_code << "): " << resp.body.substr(0, 200) << std::endl;
                    continue;
                }

                Json::Value data;
                Json::CharReaderBuilder crb;
                std::istringstream iss(resp.body);
                std::string errs;
                if (!Json::parseFromStream(crb, iss, &data, &errs)) continue;

                if (!data.isMember("ocs") || !data["ocs"]["data"].isArray()) continue;

                for (const auto& msg : data["ocs"]["data"]) {
                    int msgId = msg.isMember("id") ? msg["id"].asInt() : 0;
                    if (msgId <= conv.lastMsgId) continue;

                    conv.lastMsgId = std::max(conv.lastMsgId, msgId);

                    // Persist watermark
                    if (m_configStore) {
                        m_configStore->Set("",
                            "channel." + state->channel_name + ".polling." + token + ".last_msg_id",
                            std::to_string(conv.lastMsgId));
                    }

                    // Skip system messages
                    std::string systemMsg = msg.isMember("systemMessage") ? msg["systemMessage"].asString() : "";
                    if (!systemMsg.empty()) continue;

                    // Skip our own messages
                    std::string actorType = msg.isMember("actorType") ? msg["actorType"].asString() : "";
                    if (actorType == "bots") continue;
                    std::string actorId = msg.isMember("actorId") ? msg["actorId"].asString() : "";
                    if (actorId == username) continue;

                    std::string messageText = msg.isMember("message") ? msg["message"].asString() : "";
                    if (messageText.empty()) continue;

                    std::string actorName = msg.isMember("actorDisplayName") ? msg["actorDisplayName"].asString() : actorId;

                    // Decide whether to dispatch
                    bool shouldDispatch = false;
                    if (isDm(conv.type) && respondInDm) {
                        shouldDispatch = true;
                    } else if (!isDm(conv.type) && respondInGroupOnMention) {
                        // Check for @mention
                        if (messageText.find(mentionTrigger) != std::string::npos) {
                            shouldDispatch = true;
                        }
                    } else if (!isDm(conv.type) && !respondInGroupOnMention) {
                        // Respond to all group messages
                        shouldDispatch = true;
                    }

                    if (!shouldDispatch) continue;

                    // Strip mention trigger from message for cleaner dispatch
                    std::string cleanText = messageText;
                    if (!mentionTrigger.empty()) {
                        size_t pos;
                        while ((pos = cleanText.find(mentionTrigger)) != std::string::npos) {
                            cleanText.erase(pos, mentionTrigger.size());
                        }
                        while (!cleanText.empty() && (cleanText[0] == ' ' || cleanText[0] == '\n')) {
                            cleanText.erase(0, 1);
                        }
                    }

                    // Build prompt with context
                    std::string prompt = "[Nextcloud Talk message from " + actorName;
                    prompt += " in \"" + conv.displayName + "\"";
                    if (isDm(conv.type)) {
                        prompt += " (direct message)";
                    }
                    prompt += "]\n" + cleanText;

                    std::string sessionType = "nextcloud";

                    // Routing: per-conversation session
                    std::string routingKey = "conv:" + token;

                    // Build reply target
                    ReplyTarget replyTarget;
                    replyTarget.channel_name = state->channel_name;
                    replyTarget.channel_type = state->channel_type;
                    replyTarget.type = ReplyTarget::Chat;
                    replyTarget.peer_id = token; // conversation token for POST back

                    m_dispatch(state->agent_id, routingKey, prompt, sessionType, replyTarget);

                    std::cerr << "[nextcloud] Dispatched message from " << actorName
                              << " in " << conv.displayName << " (id=" << msgId << ")" << std::endl;
                }
            }
        } catch (const std::exception& ex) {
            std::cerr << "[nextcloud] Exception in poll loop for "
                      << state->channel_name << ": " << ex.what() << std::endl;
            state->consecutive_errors++;
            int backoff = std::min(60, state->consecutive_errors * 5);
            state->next_attempt = std::chrono::steady_clock::now() + std::chrono::seconds(backoff);
            std::this_thread::sleep_for(std::chrono::seconds(backoff));
        }
    }

    std::cerr << "[nextcloud] Talk poll loop stopped for "
              << state->channel_name << std::endl;
}

} // namespace animus::kernel

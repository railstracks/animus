#include "animus_kernel/social/SocialEventPoller.h"
#include "animus_kernel/social/TelegramTypes.h"
#include "animus_kernel/social/TelegramBotApi.h"
#include "animus_kernel/AgentConfigStore.h"
#include "animus_kernel/tools/HttpClient.h"

#include <json/json.h>
#include <json/writer.h>

#include <chrono>
#include <sstream>
#include <thread>
#include <iostream>
#include <algorithm>
#include <set>

namespace animus::kernel {

// ============================================================================
// JSON parsing helpers
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

std::string GetString(const Json::Value& v, const char* key,
                      const std::string& def = "") {
    if (v.isMember(key) && v[key].isString()) return v[key].asString();
    return def;
}

int64_t GetInt(const Json::Value& v, const char* key, int64_t def = 0) {
    if (v.isMember(key) && v[key].isInt64()) return v[key].asInt64();
    if (v.isMember(key) && v[key].isInt()) return v[key].asInt();
    return def;
}

std::string ExtractAdapterType(const std::string& platformId) {
    auto pos = platformId.find(':');
    return (pos != std::string::npos) ? platformId.substr(0, pos) : platformId;
}

} // anonymous namespace

// ============================================================================
// SocialEventRouter
// ============================================================================

SocialEventRouter::SocialEventRouter(AgentConfigStore* configStore)
    : m_configStore(configStore) {
}

std::string SocialEventRouter::MakePeerKey(const std::string& platformId,
                                           const std::string& peerId) {
    return platformId + ":peer:" + peerId;
}

std::string SocialEventRouter::MakePostKey(const std::string& platformId,
                                           const std::string& postId) {
    return platformId + ":post:" + postId;
}

void SocialEventRouter::RegisterPeer(const std::string& platformId,
                                     const std::string& peerId,
                                     const std::string& sessionKey,
                                     const std::string& agentId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RoutingEntry entry;
    entry.session_key = sessionKey;
    entry.created = std::chrono::steady_clock::now();
    entry.agent_id = agentId;
    entry.platform_id = platformId;
    m_peerMap[MakePeerKey(platformId, peerId)] = std::move(entry);
}

void SocialEventRouter::RegisterPost(const std::string& platformId,
                                     const std::string& postId,
                                     const std::string& sessionKey,
                                     const std::string& agentId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RoutingEntry entry;
    entry.session_key = sessionKey;
    entry.created = std::chrono::steady_clock::now();
    entry.agent_id = agentId;
    entry.platform_id = platformId;
    m_postMap[MakePostKey(platformId, postId)] = std::move(entry);
}

std::optional<SocialEventRouter::RoutingEntry>
SocialEventRouter::LookupPeer(const std::string& platformId,
                              const std::string& peerId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_peerMap.find(MakePeerKey(platformId, peerId));
    if (it == m_peerMap.end()) return std::nullopt;
    return it->second;
}

std::optional<SocialEventRouter::RoutingEntry>
SocialEventRouter::LookupPost(const std::string& platformId,
                              const std::string& postId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_postMap.find(MakePostKey(platformId, postId));
    if (it == m_postMap.end()) return std::nullopt;
    return it->second;
}

void SocialEventRouter::PruneExpired(std::chrono::seconds ttl) {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto it = m_peerMap.begin(); it != m_peerMap.end(); ) {
        if (now - it->second.created > ttl) {
            it = m_peerMap.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = m_postMap.begin(); it != m_postMap.end(); ) {
        if (now - it->second.created > ttl) {
            it = m_postMap.erase(it);
        } else {
            ++it;
        }
    }
}

void SocialEventRouter::SaveState() {
    if (!m_configStore) return;
    std::lock_guard<std::mutex> lock(m_mutex);

    const std::string agentId; // TODO: derive from routing entry or accept as parameter

    // Clear existing routing state
    m_configStore->DeleteByPrefix(agentId, "social.routing.");

    // Save peer mappings
    int idx = 0;
    for (const auto& [key, entry] : m_peerMap) {
        Json::Value obj(Json::objectValue);
        obj["platform_id"] = entry.platform_id;
        obj["session_key"] = entry.session_key;
        obj["agent_id"] = entry.agent_id;
        // Store the raw key so we can reconstruct
        // key format: "platformId:peer:peerId"
        obj["routing_key"] = key;
        obj["type"] = "peer";

        Json::StreamWriterBuilder wb;
        wb.settings_["indentation"] = "";
        m_configStore->Set(agentId,
            "social.routing." + std::to_string(idx++),
            Json::writeString(wb, obj));
    }

    // Save post mappings
    for (const auto& [key, entry] : m_postMap) {
        Json::Value obj(Json::objectValue);
        obj["platform_id"] = entry.platform_id;
        obj["session_key"] = entry.session_key;
        obj["agent_id"] = entry.agent_id;
        obj["routing_key"] = key;
        obj["type"] = "post";

        Json::StreamWriterBuilder wb;
        wb.settings_["indentation"] = "";
        m_configStore->Set(agentId,
            "social.routing." + std::to_string(idx++),
            Json::writeString(wb, obj));
    }
}

void SocialEventRouter::LoadState() {
    if (!m_configStore) return;
    std::lock_guard<std::mutex> lock(m_mutex);

    const std::string agentId; // TODO: derive from routing entry or accept as parameter
    auto keys = m_configStore->ListKeys(agentId);

    for (const auto& key : keys) {
        if (key.substr(0, 15) != "social.routing.") continue;

        auto val = m_configStore->Get(agentId, key);
        auto obj = ParseJson(val);
        if (obj.isNull()) continue;

        std::string routingKey = GetString(obj, "routing_key");
        std::string type = GetString(obj, "type");
        std::string platformId = GetString(obj, "platform_id");
        std::string sessionKey = GetString(obj, "session_key");
        std::string agentIdEntry = GetString(obj, "agent_id", "");

        RoutingEntry entry;
        entry.platform_id = platformId;
        entry.session_key = sessionKey;
        entry.agent_id = agentIdEntry;
        entry.created = std::chrono::steady_clock::now(); // Reset TTL on load

        if (type == "peer") {
            m_peerMap[routingKey] = std::move(entry);
        } else if (type == "post") {
            m_postMap[routingKey] = std::move(entry);
        }
    }
}

// ============================================================================
// SocialEventPoller
// ============================================================================

SocialEventPoller::SocialEventPoller(
    HttpClient& httpClient,
    AgentConfigStore* configStore,
    SocialEventRouter& router,
    DispatchCallback dispatch)
    : m_httpClient(httpClient)
    , m_configStore(configStore)
    , m_router(router)
    , m_dispatch(std::move(dispatch)) {
}

SocialEventPoller::~SocialEventPoller() {
    Stop();
}

void SocialEventPoller::AddInstance(const PollerConfig& config) {
    if (m_instances.count(config.platform_id)) return;

    auto state = std::make_unique<InstanceState>();
    state->config = config;
    state->next_attempt = std::chrono::steady_clock::now();

    // Load credentials from config store
    if (m_configStore) {
        const std::string agentId; // TODO: derive from routing entry or accept as parameter
        state->access_token = m_configStore->Get(agentId,
            "social." + config.platform_id + ".access_token");
        state->group_id = m_configStore->Get(agentId,
            "social." + config.platform_id + ".group_id");

        // Load persisted Long Poll state
        state->lp_ts = m_configStore->Get(agentId,
            "social." + config.platform_id + ".polling.ts");
        state->lp_key = m_configStore->Get(agentId,
            "social." + config.platform_id + ".polling.key");
        state->lp_server = m_configStore->Get(agentId,
            "social." + config.platform_id + ".polling.server");
    }

    m_instances[config.platform_id] = std::move(state);
}

bool SocialEventPoller::Start() {
    if (m_running) return true;

    // Filter instances that have polling enabled and credentials
    for (auto& [platformId, state] : m_instances) {
        if (state->access_token.empty()) {
            std::cerr << "[social:poller] Skipping " << platformId
                      << ": no access token\n";
            continue;
        }

        if (state->config.method != "long_poll" && state->config.method != "rest") {
            std::cerr << "[social:poller] Skipping " << platformId
                      << ": unknown method '" << state->config.method << "'\n";
            continue;
        }

        state->active = true;

        if (state->config.adapter_type == "telegram") {
            state->thread = std::thread(&SocialEventPoller::TelegramLongPollLoop, this, state.get());
        } else if (state->config.method == "long_poll") {
            state->thread = std::thread(&SocialEventPoller::LongPollLoop, this, state.get());
        } else {
            state->thread = std::thread(&SocialEventPoller::RestPollLoop, this, state.get());
        }

        std::cerr << "[social:poller] Started " << state->config.method
                  << " poller for " << platformId << "\n";
    }

    if (m_instances.empty()) return false;

    m_running = true;
    return true;
}

void SocialEventPoller::Stop() {
    if (!m_running) return;

    m_stopRequested = true;

    for (auto& [platformId, state] : m_instances) {
        state->active = false;
    }

    for (auto& [platformId, state] : m_instances) {
        if (state->thread.joinable()) {
            state->thread.join();
        }
    }

    // Save routing state for persistence across restarts
    m_router.SaveState();

    m_running = false;
    m_stopRequested = false;
}

// ============================================================================
// VK Long Poll Loop
// ============================================================================

bool SocialEventPoller::FetchLongPollServer(InstanceState* state) {
    // Call VK API: groups.getLongPollServer
    std::string url = "https://api.vk.ru/method/groups.getLongPollServer?"
        "group_id=" + state->group_id +
        "&v=5.131&access_token=" + state->access_token;

    HttpClient::Request req;
    req.url = url;
    req.timeout_seconds = 10;

    auto resp = m_httpClient.Execute(req);
    if (resp.status_code != 200) {
        std::cerr << "[social:poller] getLongPollServer HTTP error: "
                  << resp.status_code << "\n";
        return false;
    }

    auto json = ParseJson(resp.body);
    if (json.isMember("error")) {
        std::cerr << "[social:poller] getLongPollServer API error: "
                  << json["error"].toStyledString() << "\n";
        return false;
    }

    auto& response = json["response"];
    state->lp_server = GetString(response, "server");
    state->lp_key = GetString(response, "key");
    state->lp_ts = GetString(response, "ts");

    if (state->lp_server.empty() || state->lp_key.empty()) {
        std::cerr << "[social:poller] getLongPollServer returned empty fields\n";
        return false;
    }

    // Persist for restart recovery
    if (m_configStore) {
        const std::string agentId; // TODO: derive from routing entry or accept as parameter
        std::string prefix = "social." + state->config.platform_id + ".polling.";
        m_configStore->Set(agentId, prefix + "server", state->lp_server);
        m_configStore->Set(agentId, prefix + "key", state->lp_key);
        m_configStore->Set(agentId, prefix + "ts", state->lp_ts);
    }

    std::cerr << "[social:poller] Long Poll server acquired for "
              << state->config.platform_id << " (ts=" << state->lp_ts << ")\n";
    return true;
}

std::string SocialEventPoller::BuildLongPollUrl(const InstanceState* state) const {
    return state->lp_server +
        "?act=a_check&key=" + state->lp_key +
        "&ts=" + state->lp_ts +
        "&wait=" + std::to_string(state->config.wait_seconds);
}

void SocialEventPoller::LongPollLoop(InstanceState* state) {
    std::cerr << "[social:poller] Long Poll loop starting for "
              << state->config.platform_id << "\n";

    // Initial server fetch
    if (!FetchLongPollServer(state)) {
        std::cerr << "[social:poller] Initial Long Poll server fetch failed for "
                  << state->config.platform_id << "\n";
        state->active = false;
        return;
    }

    while (state->active && !m_stopRequested) {
        try {
        // Check backoff
        auto now = std::chrono::steady_clock::now();
        if (now < state->next_attempt) {
            auto sleepMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                state->next_attempt - now).count();
            std::this_thread::sleep_for(std::chrono::milliseconds(
                std::min(sleepMs, (int64_t)5000)));
            continue;
        }

        // Make the Long Poll request
        HttpClient::Request req;
        req.url = BuildLongPollUrl(state);
        req.timeout_seconds = state->config.wait_seconds + 10; // wait + buffer

        std::string responseBody;
        int responseCode = 0;
        try {
            auto resp2 = m_httpClient.Execute(req);
            responseBody = resp2.body;
            responseCode = resp2.status_code;
        } catch (const std::exception& ex) {
            std::cerr << "[social:poller] Long Poll HTTP exception: "
                      << ex.what() << " for " << state->config.platform_id << "\n";
            state->consecutive_errors++;
            int backoff = std::min(30, state->consecutive_errors * 5);
            state->next_attempt = now + std::chrono::seconds(backoff);
            continue;
        }

        if (m_stopRequested) break;

        if (responseCode != 200) {
            state->consecutive_errors++;
            int backoff = std::min(30, state->consecutive_errors * 5);
            std::cerr << "[social:poller] Long Poll HTTP error "
                      << responseCode << " for " << state->config.platform_id
                      << " (backoff " << backoff << "s)\n";
            state->next_attempt = now + std::chrono::seconds(backoff);
            continue;
        }

        auto json = ParseJson(responseBody);

        // Handle VK Long Poll failures
        if (json.isMember("failed")) {
            int failed = GetInt(json, "failed");
            std::cerr << "[social:poller] Long Poll failure code " << failed
                      << " for " << state->config.platform_id << "\n";

            if (failed == 1) {
                // History outdated — update ts and continue
                state->lp_ts = GetString(json, "ts", state->lp_ts);
                state->consecutive_errors = 0;
                continue;
            }

            if (failed == 2 || failed == 3) {
                // Key expired or info lost — re-fetch
                if (!FetchLongPollServer(state)) {
                    state->consecutive_errors++;
                    int backoff = std::min(60, state->consecutive_errors * 10);
                    state->next_attempt = now + std::chrono::seconds(backoff);
                } else {
                    state->consecutive_errors = 0;
                }
                continue;
            }

            // Unknown failure
            state->consecutive_errors++;
            state->next_attempt = now + std::chrono::seconds(30);
            continue;
        }

        // Success — extract events
        state->consecutive_errors = 0;
        state->lp_ts = GetString(json, "ts", state->lp_ts);

        // Persist the new ts
        if (m_configStore) {
            m_configStore->Set("",
                "social." + state->config.platform_id + ".polling.ts",
                state->lp_ts);
        }

        auto& updates = json["updates"];
        if (updates.isArray()) {
            for (const auto& update : updates) {
                try {
                    std::string eventType = GetString(update, "type");
                    if (eventType.empty()) continue;

                    // Filter to configured event types
                    if (!state->config.events.empty()) {
                        bool found = false;
                        for (const auto& e : state->config.events) {
                            if (e == eventType) { found = true; break; }
                        }
                        if (!found) continue;
                    }

                    // Serialize the object for processing
                    Json::StreamWriterBuilder wb;
                    wb.settings_["indentation"] = "";
                    std::string objectJson = Json::writeString(wb, update["object"]);

                    ProcessVKEvent(state, eventType, objectJson);
                } catch (const std::exception& ex) {
                    std::cerr << "[social:poller] Exception processing event: "
                              << ex.what() << "\n";
                }
            }
        }

        // Prune expired sessions periodically
        m_router.PruneExpired(state->config.session_ttl);

        } catch (const std::exception& ex) {
            std::cerr << "[social:poller] Exception in Long Poll loop: "
                      << ex.what() << " for " << state->config.platform_id << "\n";
            state->consecutive_errors++;
            state->next_attempt = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        }
    }

    std::cerr << "[social:poller] Long Poll loop ended for "
              << state->config.platform_id << "\n";
}

// ============================================================================
// REST Poll Loop (placeholder for Bluesky, etc.)
// ============================================================================

void SocialEventPoller::RestPollLoop(InstanceState* state) {
    std::cerr << "[social:poller] REST poll loop starting for "
              << state->config.platform_id << "\n";

    while (state->active && !m_stopRequested) {
        auto now = std::chrono::steady_clock::now();
        if (now < state->next_attempt) {
            auto sleepMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                state->next_attempt - now).count();
            std::this_thread::sleep_for(std::chrono::milliseconds(
                std::min(sleepMs, (int64_t)60000)));
            continue;
        }

        // TODO: Implement REST polling for Bluesky notifications
        // This will use app.bsky.notification.listNotifications

        state->next_attempt = now + std::chrono::seconds(state->config.interval_seconds);
    }

    std::cerr << "[social:poller] REST poll loop ended for "
              << state->config.platform_id << "\n";
}

// ============================================================================
// VK Event Processing
// ============================================================================

void SocialEventPoller::ProcessVKEvent(InstanceState* state,
                                        const std::string& eventType,
                                        const std::string& objectJson) {
    if (eventType == "message_new") {
        ProcessVKMessageNew(state, objectJson);
    } else if (eventType == "wall_post_new") {
        ProcessVKWallPostNew(state, objectJson);
    } else if (eventType == "wall_reply_new") {
        ProcessVKWallReplyNew(state, objectJson);
    }
    // Other events (like_add, message_typing_state, etc.) are silently ignored
}

void SocialEventPoller::ProcessVKMessageNew(InstanceState* state,
                                             const std::string& objectJson) {
    auto obj = ParseJson(objectJson);
    if (obj.isNull()) return;

    // VK API >= 5.103: message is nested under "message" key
    auto& msg = obj.isMember("message") ? obj["message"] : obj;

    std::string peerId = std::to_string(GetInt(msg, "peer_id"));
    std::string fromId = std::to_string(GetInt(msg, "from_id"));
    std::string text = GetString(msg, "text");
    int64_t messageId = GetInt(msg, "id");

    // Skip our own messages
    int64_t groupId = 0;
    try { groupId = std::stoll(state->group_id); } catch (...) {}
    int64_t fromIdVal = std::stoll(fromId);
    if (fromIdVal == -groupId || fromIdVal == groupId) {
        std::cerr << "[social:poller] Skipping own event: from=" << fromId << " group=" << groupId << std::endl;
        return;
    }

    // Deduplicate — skip if we've already processed this message
    if (state->seenEventIds.count(messageId)) {
        std::cerr << "[social:poller] VK message_new: skipping duplicate id=" << messageId << std::endl;
        return;
    }
    state->seenEventIds.insert(messageId);
    if (state->seenEventIds.size() > InstanceState::kMaxSeenIds) {
        state->seenEventIds.erase(state->seenEventIds.begin());
    }

    std::cerr << "[social:poller] VK message_new: peer=" << peerId
              << " from=" << fromId << " text='" << text << "'\n";

    // Resolve user name
    auto names = ResolveVKUsers(state, {fromId});
    std::string displayName = names.count(fromId) ? names[fromId] : fromId;

    // Build the formatted message
    std::string formattedMsg = displayName + " (" + fromId + "): " + text;

    // Fetch recent chat history for context
    std::string history = FetchVKChatHistory(state, peerId, 10);

    // Build the full prompt
    // Include platform_id and peer_id so the agent knows which instance
    // and conversation to use for replies
    std::string replyHint = "\n\nRespond naturally — your reply will be sent automatically.";

    std::string prompt;
    if (history.empty()) {
        prompt = "New message in your VK community chat (platform_id: " +
                 state->config.platform_id + ", peer_id: " + peerId + "):\n\n" +
                 formattedMsg + replyHint;
    } else {
        prompt = "New message in your VK community chat (platform_id: " +
                 state->config.platform_id + ", peer_id: " + peerId + "):\n\n"
                 "--- Recent conversation ---\n" + history +
                 "\n--- End history ---\n\n"
                 "New message:\n" + formattedMsg + replyHint;
    }

    // Dispatch to session — for DMs (peer is a user ID), also try routing
    // by from_id in case the user started a new DM thread
    DispatchToSession(state, "peer:" + peerId, prompt, "chat");
}

void SocialEventPoller::ProcessVKWallPostNew(InstanceState* state,
                                              const std::string& objectJson) {
    auto obj = ParseJson(objectJson);
    if (obj.isNull()) return;

    std::string postId = std::to_string(GetInt(obj, "id"));
    std::string fromId = std::to_string(GetInt(obj, "from_id"));
    std::string text = GetString(obj, "text");

    // Skip bot's own posts
    int64_t groupId = 0;
    try { groupId = std::stoll(state->group_id); } catch (...) {}
    int64_t fromIdVal = std::stoll(fromId);
    if (fromIdVal == -groupId || fromIdVal == groupId) {
        std::cerr << "[social:poller] Skipping own event: from=" << fromId << " group=" << groupId << std::endl;
        return;
    }

    // Deduplicate
    int64_t postIdInt = GetInt(obj, "id");
    if (state->seenEventIds.count(postIdInt)) return;
    state->seenEventIds.insert(postIdInt);
    if (state->seenEventIds.size() > InstanceState::kMaxSeenIds) {
        state->seenEventIds.erase(state->seenEventIds.begin());
    }

    std::cerr << "[social:poller] VK wall_post_new: post=" << postId
              << " from=" << fromId << "\n";

    // Resolve user name
    auto names = ResolveVKUsers(state, {fromId});
    std::string displayName = names.count(fromId) ? names[fromId] : fromId;

    std::string prompt = "New post on your VK community wall:\n\n" +
        displayName + " (" + fromId + ") posted:\n" + text +
        "\n\nRespond naturally — your reply will be posted as a comment automatically.";

    DispatchToSession(state, "post:" + postId, prompt, "wall");
}

void SocialEventPoller::ProcessVKWallReplyNew(InstanceState* state,
                                               const std::string& objectJson) {
    auto obj = ParseJson(objectJson);
    if (obj.isNull()) return;

    std::string postId = std::to_string(GetInt(obj, "post_id"));
    std::string fromId = std::to_string(GetInt(obj, "from_id"));
    std::string text = GetString(obj, "text");
    std::string replyId = std::to_string(GetInt(obj, "id"));
    std::string replyToComment = std::to_string(GetInt(obj, "reply_to_comment"));

    // Skip bot's own replies
    int64_t groupId = 0;
    try { groupId = std::stoll(state->group_id); } catch (...) {}
    int64_t fromIdVal = std::stoll(fromId);
    if (fromIdVal == -groupId || fromIdVal == groupId) {
        std::cerr << "[social:poller] Skipping own event: from=" << fromId << " group=" << groupId << std::endl;
        return;
    }

    // Deduplicate
    int64_t replyIdInt = GetInt(obj, "id");
    if (state->seenEventIds.count(replyIdInt)) return;
    state->seenEventIds.insert(replyIdInt);
    if (state->seenEventIds.size() > InstanceState::kMaxSeenIds) {
        state->seenEventIds.erase(state->seenEventIds.begin());
    }

    std::cerr << "[social:poller] VK wall_reply_new: post=" << postId
              << " from=" << fromId << " reply_to=" << replyToComment << "\n";

    // Resolve user name
    auto names = ResolveVKUsers(state, {fromId});
    std::string displayName = names.count(fromId) ? names[fromId] : fromId;

    std::string prompt = "New comment on your VK community wall post (post_id=" +
        postId + "):\n\n" +
        displayName + " (" + fromId + "): " + text +
        "\n\nRespond naturally — your reply will be posted as a comment automatically.";

    // Route by post+comment for threading: if replying to a specific comment,
    // create a sub-session per comment thread. Otherwise route by post only.
    std::string routingKey = "post:" + postId;
    if (!replyToComment.empty() && replyToComment != "0") {
        routingKey = "post:" + postId + ":comment:" + replyToComment;
    }

    DispatchToSession(state, routingKey, prompt, "wall");
}

// ============================================================================
// User Resolution
// ============================================================================

std::unordered_map<std::string, std::string>
SocialEventPoller::ResolveVKUsers(InstanceState* state,
                                   const std::vector<std::string>& userIds) {
    std::unordered_map<std::string, std::string> result;

    if (userIds.empty()) return result;

    // Batch resolve: users.get?user_ids=id1,id2,...
    std::string ids;
    for (size_t i = 0; i < userIds.size(); ++i) {
        if (i > 0) ids += ",";
        ids += userIds[i];
    }

    std::string url = "https://api.vk.ru/method/users.get?"
        "user_ids=" + ids +
        "&fields=first_name,last_name"
        "&v=5.131&access_token=" + state->access_token;

    HttpClient::Request req;
    req.url = url;
    req.timeout_seconds = 10;

    auto resp = m_httpClient.Execute(req);
    if (resp.status_code != 200) {
        std::cerr << "[social:poller] users.get HTTP error: "
                  << resp.status_code << "\n";
        return result;
    }

    auto json = ParseJson(resp.body);
    if (json.isMember("error")) {
        std::cerr << "[social:poller] users.get API error\n";
        return result;
    }

    auto& response = json["response"];
    if (response.isArray()) {
        for (const auto& user : response) {
            std::string uid = std::to_string(GetInt(user, "id"));
            std::string firstName = GetString(user, "first_name");
            std::string lastName = GetString(user, "last_name");
            result[uid] = firstName + " " + lastName;
        }
    }

    return result;
}

// ============================================================================
// Chat History Fetch
// ============================================================================

std::string SocialEventPoller::FetchVKChatHistory(InstanceState* state,
                                                    const std::string& peerId,
                                                    int count) {
    std::string url = "https://api.vk.ru/method/messages.getHistory?"
        "peer_id=" + peerId +
        "&count=" + std::to_string(count) +
        "&v=5.131&access_token=" + state->access_token;

    HttpClient::Request req;
    req.url = url;
    req.timeout_seconds = 10;

    auto resp = m_httpClient.Execute(req);
    if (resp.status_code != 200) return "";

    auto json = ParseJson(resp.body);
    if (json.isMember("error")) return "";

    auto& items = json["response"]["items"];
    if (!items.isArray() || items.empty()) return "";

    // Collect user IDs for resolution
    std::vector<std::string> fromIds;
    std::set<std::string> seen;
    for (const auto& msg : items) {
        std::string fid = std::to_string(GetInt(msg, "from_id"));
        if (seen.insert(fid).second) {
            fromIds.push_back(fid);
        }
    }

    auto names = ResolveVKUsers(state, fromIds);

    // Add bot's own name for negative IDs (group messages)
    std::string botName = "Bot";
    if (!state->group_id.empty()) botName = "Bot (" + state->group_id + ")";

    // Format the history
    std::string history;
    for (const auto& msg : items) {
        std::string fid = std::to_string(GetInt(msg, "from_id"));
        std::string name;
        if (fid[0] == '-') {
            name = botName;
        } else {
            name = names.count(fid) ? names[fid] : fid;
        }
        std::string text = GetString(msg, "text");

        // Skip system messages (chat_create, etc.)
        if (text.empty() && msg.isMember("action")) continue;

        if (!history.empty()) history += "\n";
        history += name + " (" + fid + "): " + text;
    }

    return history;
}

// ============================================================================
// Session Dispatch
// ============================================================================

void SocialEventPoller::DispatchToSession(InstanceState* state,
                                           const std::string& routingKey,
                                           const std::string& message,
                                           const std::string& sessionType) {
    // Determine if this is a peer or post routing
    bool isPeer = (routingKey.size() > 5 && routingKey.substr(0, 5) == "peer:");
    bool isPost = (routingKey.size() > 5 && routingKey.substr(0, 5) == "post:");

    std::string routingValue = routingKey.substr(5); // Strip "peer:" or "post:" prefix

    // Look up existing session
    std::optional<SocialEventRouter::RoutingEntry> entry;
    if (isPeer) {
        entry = m_router.LookupPeer(state->config.platform_id, routingValue);
    } else if (isPost) {
        entry = m_router.LookupPost(state->config.platform_id, routingValue);
    }

    std::string sessionKey;
    if (entry) {
        sessionKey = entry->session_key;
    } else {
        // Create a new session key
        // Format: "{sessionType}:{platformId}:{routingValue}"
        // (no "social:" prefix — AgentKernel adds that)
        sessionKey = sessionType + ":" +
                     state->config.platform_id + ":" + routingValue;

        // Register in routing table
        if (isPeer) {
            m_router.RegisterPeer(state->config.platform_id, routingValue,
                                  sessionKey, "");
        } else if (isPost) {
            m_router.RegisterPost(state->config.platform_id, routingValue,
                                  sessionKey, "");
        }
    }

    // Build auto-reply target
    ReplyTarget replyTarget;
    replyTarget.platform_id = state->config.platform_id;
    replyTarget.adapter_type = state->config.adapter_type;
    replyTarget.group_id = state->group_id;

    if (isPeer) {
        replyTarget.type = ReplyTarget::Chat;
        replyTarget.peer_id = routingValue;
    } else if (isPost) {
        replyTarget.type = ReplyTarget::Wall;
        // Parse routing value: "post:123" or "post:123:comment:456"
        size_t commentPos = routingValue.find(":comment:");
        if (commentPos != std::string::npos) {
            replyTarget.post_id = routingValue.substr(0, commentPos);
            replyTarget.reply_to_comment = routingValue.substr(commentPos + 9);
        } else {
            replyTarget.post_id = routingValue;
        }
    }

    // Dispatch to the chain runner via callback
    m_dispatch(entry ? entry->agent_id : "", sessionKey, message, sessionType, replyTarget);
}

// ============================================================================
// Telegram Long Poll Loop
// ============================================================================

void SocialEventPoller::TelegramLongPollLoop(InstanceState* state) {
    std::cerr << "[telegram:poller] Long Poll loop starting for "
              << state->config.platform_id << std::endl;

    // Create the Bot API client
    telegram::TelegramBotApi api(m_httpClient);

    // Verify bot token with getMe
    auto botInfo = api.GetMe(state->access_token);
    if (!botInfo) {
        std::cerr << "[telegram:poller] getMe failed for " << state->config.platform_id
                  << " — check bot token" << std::endl;
        state->active = false;
        return;
    }
    std::cerr << "[telegram:poller] Connected as @" << botInfo->username
              << " (" << botInfo->first_name << ", id=" << botInfo->id << ")" << std::endl;

    // Restore last_update_id from config store for resume
    int64_t lastUpdateId = 0;
    if (m_configStore) {
        std::string stored = m_configStore->Get("",
            "social." + state->config.platform_id + ".last_update_id");
        if (!stored.empty()) {
            try { lastUpdateId = std::stoll(stored); } catch (...) {}
        }
    }

    // Determine allowed updates
    std::vector<std::string> allowedUpdates = {"message", "edited_message",
        "my_chat_member", "callback_query"};
    if (!state->config.events.empty()) {
        allowedUpdates = state->config.events;
    }

    while (state->active && !m_stopRequested) {
        try {
            // Backoff check
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
            opts.offset = (lastUpdateId > 0) ? lastUpdateId + 1 : 0;
            opts.timeout = state->config.wait_seconds;
            opts.allowed_updates = allowedUpdates;

            auto updates = api.GetUpdates(state->access_token, opts);

            if (m_stopRequested) break;

            // Process updates
            for (auto& update : updates) {
                lastUpdateId = std::max(lastUpdateId, update.update_id);

                switch (update.type) {
                    case telegram::Update::Type::Message:
                    case telegram::Update::Type::EditedMessage:
                        ProcessTelegramMessage(state, update.message, api);
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
                m_configStore->Set("",
                    "social." + state->config.platform_id + ".last_update_id",
                    std::to_string(lastUpdateId));
            }

            state->consecutive_errors = 0;

        } catch (const std::exception& ex) {
            std::cerr << "[telegram:poller] Exception in loop for "
                      << state->config.platform_id << ": " << ex.what() << std::endl;
            state->consecutive_errors++;
            int backoff = std::min(60, state->consecutive_errors * 5);
            state->next_attempt = std::chrono::steady_clock::now() + std::chrono::seconds(backoff);
        }
    }

    std::cerr << "[telegram:poller] Loop ended for " << state->config.platform_id << std::endl;
}

void SocialEventPoller::ProcessTelegramMessage(
        InstanceState* state,
        const telegram::Message& msg,
        telegram::TelegramBotApi& api) {

    // Skip empty messages (media-only, stickers, etc. without text)
    if (msg.text.empty()) return;

    // Build routing key
    // For private chats: "peer:{chat_id}"
    // For groups: "peer:{chat_id}:{thread_id}"
    std::string routingKey = "peer:" + std::to_string(msg.chat.id);
    if (msg.message_thread_id > 0) {
        routingKey += ":" + std::to_string(msg.message_thread_id);
    }

    // Build prompt
    std::string senderName = msg.from.DisplayName();
    std::string chatName = msg.chat.DisplayName();
    std::string prompt;

    if (msg.chat.IsPrivate()) {
        prompt = "New message from " + senderName + ":\n" + msg.text;
    } else {
        prompt = "Message from " + senderName + " in " + chatName;
        if (msg.message_thread_id > 0) {
            prompt += " (topic thread)";
        }
        prompt += ":\n" + msg.text;
    }

    // Determine session type
    std::string sessionType = msg.chat.IsPrivate() ? "telegram:private" : "telegram:group";

    DispatchToSession(state, routingKey, prompt, sessionType);
}

void SocialEventPoller::ProcessTelegramChatMemberUpdate(
        InstanceState* state,
        const telegram::Update& update) {

    std::string status = update.member_status;
    std::string chatId = std::to_string(update.member_chat_id);

    if (status == "member" || status == "administrator") {
        std::cerr << "[telegram:poller] Bot added to chat " << chatId
                  << " (status: " << status << ")" << std::endl;
    } else if (status == "left" || status == "kicked") {
        std::cerr << "[telegram:poller] Bot removed from chat " << chatId
                  << " (status: " << status << ")" << std::endl;
        // Clean up routing entries for this chat
    }
}

} // namespace animus::kernel

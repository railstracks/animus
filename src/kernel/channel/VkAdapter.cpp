#include "animus_kernel/Log.h"
#include "animus_kernel/ChannelAdapters.h"
#include "animus_kernel/ChannelContext.h"
#include "animus_kernel/ChannelHelpers.h"

#include <iostream>
#include <random>

#include "animus_kernel/ChannelState.h"
#include "animus_kernel/AgentConfigStore.h"
#include "animus_kernel/ChannelManager.h"
#include "animus_kernel/tools/HttpClient.h"

namespace animus::kernel {

using namespace channel_detail;

// ============================================================================
// VkAdapter
// ============================================================================

void VkAdapter::RunLoop() {
    auto* rt = m_runtime.get();
    ALOG_DEBUG("vk", "Long Poll loop starting for " << rt->channel_name);

    std::string token = GetString(rt->config, "access_token");
    if (token.empty()) {
        ALOG_WARNING("vk", "No access token for " << rt->channel_name);
        rt->active = false;
        return;
    }

    rt->group_id = GetString(rt->config, "group_id");

    if (rt->lp_server.empty()) {
        if (!FetchLongPollServer()) {
            ALOG_WARNING("vk", "Failed to get Long Poll server for " << rt->channel_name);
            rt->active = false;
            return;
        }
    }

    while (rt->active && !m_stopRequested) {
        auto now = std::chrono::steady_clock::now();
        if (now < rt->next_attempt) {
            auto sleepMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                rt->next_attempt - now).count();
            std::this_thread::sleep_for(std::chrono::milliseconds(
                std::min(sleepMs, (int64_t)60000)));
            continue;
        }

        std::string url = BuildLongPollUrl();
        int waitSeconds = GetInt(rt->config, "polling.wait", 25);

        HttpClient::Request req;
        req.url = url + "&wait=" + std::to_string(waitSeconds) + "&mode=2";
        req.timeout_seconds = waitSeconds + 15;

        auto resp = m_ctx.httpClient.Execute(req);
        if (m_stopRequested) break;

        if (resp.status_code != 200) {
            rt->consecutive_errors++;
            rt->next_attempt = now + std::chrono::seconds(30);
            continue;
        }

        auto json = ParseJson(resp.body);

        if (json.isMember("failed")) {
            int failed = json.get("failed", 0).asInt();
            if (failed == 1) {
                rt->lp_ts = GetString(json, "ts", rt->lp_ts);
                continue;
            } else {
                if (!FetchLongPollServer()) {
                    rt->consecutive_errors++;
                    rt->next_attempt = now + std::chrono::seconds(30);
                    continue;
                }
                continue;
            }
        }

        rt->consecutive_errors = 0;
        rt->lp_ts = GetString(json, "ts", rt->lp_ts);

        if (m_ctx.configStore) {
            m_ctx.configStore->Set("", "channel." + rt->channel_name + ".polling.ts", rt->lp_ts);
        }

        auto& updates = json["updates"];
        if (updates.isArray()) {
            for (const auto& update : updates) {
                try {
                    std::string eventType = GetString(update, "type");
                    if (eventType.empty()) continue;
                    std::string objectJson = JsonCompact(update["object"]);
                    ProcessEvent(eventType, objectJson);
                } catch (const std::exception& ex) {
                    ALOG_ERROR("vk", "Exception processing event: " << ex.what());
                }
            }
        }

        m_ctx.router.PruneExpired(std::chrono::seconds(86400));
    }

    ALOG_DEBUG("vk", "Long Poll loop ended for " << rt->channel_name);
}

void VkAdapter::ProcessEvent(const std::string& eventType, const std::string& objectJson) {
    if (eventType == "message_new") {
        ProcessMessageNew(objectJson);
    } else if (eventType == "wall_post_new") {
        ProcessWallPostNew(objectJson);
    } else if (eventType == "wall_reply_new") {
        ProcessWallReplyNew(objectJson);
    }
}

void VkAdapter::ProcessMessageNew(const std::string& objectJson) {
    auto* rt = m_runtime.get();
    auto obj = ParseJson(objectJson);
    if (obj.isNull()) return;

    auto& msg = obj.isMember("message") ? obj["message"] : obj;

    std::string peerId = std::to_string(GetInt(msg, "peer_id"));
    std::string fromId = std::to_string(GetInt(msg, "from_id"));
    std::string text = GetString(msg, "text");
    int64_t messageId = GetInt(msg, "id");

    int64_t groupId = 0;
    try { groupId = std::stoll(rt->group_id); } catch (...) {}
    int64_t fromIdVal = std::stoll(fromId);
    if (fromIdVal == -groupId || fromIdVal == groupId) {
        ALOG_DEBUG("vk", "chat message from the community itself (from_id="
                  << fromId << ") — skipping");
        return;
    }

    if (!rt->RememberEvent(messageId)) return;

    ALOG_DEBUG("vk", "message_new: peer=" << peerId << " from=" << fromId);

    auto names = ResolveUsers({fromId});
    std::string displayName = names.count(fromId) ? names[fromId] : fromId;

    std::string history = FetchChatHistory(peerId, 10);

    // Body: attribution header only — ids and delivery semantics live in
    // the context card (#15). Tool-only delivery (Aug 31): community chats
    // can be multi-user, silence must be first-class.
    std::string prompt = "VK chat message from " + displayName + ":\n";
    if (!history.empty())
        prompt += "\n--- Recent conversation ---\n" + history +
                  "\n--- End history ---\n\n";
    prompt += text;

    Json::Value meta;
    meta["message_type"] = "chat";
    meta["delivery"] = "tool";
    Json::Value origin;
    origin["user"] = displayName;
    origin["user_id"] = fromId; // stable VK id (#42 person-graph key)
    meta["origin"] = origin;
    if (messageId > 0)
        meta["source_message_id"] = std::to_string(messageId);
    meta["reply_instructions"] =
        "Reply using the channels tool with action=reply; peer_id is provided "
        "by the reply target and filled automatically. Text replies are NOT "
        "delivered. If the message is not addressed to you, staying silent "
        "is correct.";
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    std::string metadata = Json::writeString(wb, meta);

    ALOG_INFO("vk", "dispatching chat message from " << displayName
              << " for " << rt->channel_name);

    Dispatch("peer:" + peerId, prompt, "chat", metadata);
}

void VkAdapter::ProcessWallPostNew(const std::string& objectJson) {
    auto* rt = m_runtime.get();
    auto obj = ParseJson(objectJson);
    if (obj.isNull()) return;

    std::string postId = std::to_string(GetInt(obj, "id"));
    std::string fromId = std::to_string(GetInt(obj, "from_id"));
    std::string text = GetString(obj, "text");

    int64_t groupId = 0;
    try { groupId = std::stoll(rt->group_id); } catch (...) {}
    int64_t fromIdVal = std::stoll(fromId);
    if (fromIdVal == -groupId || fromIdVal == groupId) {
        // Community-authored (admin posting "as community" or bot output):
        // anti-loop skip, but visible — silence cost us an hour tonight.
        ALOG_DEBUG("vk", "wall post from the community itself (from_id="
                  << fromId << ") — skipping");
        return;
    }

    int64_t postIdInt = GetInt(obj, "id");
    if (!rt->RememberEvent(postIdInt)) return;

    auto names = ResolveUsers({fromId});
    std::string displayName = names.count(fromId) ? names[fromId] : fromId;

    // Body: attribution header + content only (ids in the card).
    std::string prompt = "VK wall post from " + displayName + ":\n" + text;

    Json::Value meta;
    meta["message_type"] = "wall";
    Json::Value origin;
    origin["user"] = displayName;
    origin["user_id"] = fromId;
    origin["channel"] = "wall";
    meta["origin"] = origin;
    meta["post_id"] = postId;
    meta["source_message_id"] = postId;
    meta["reply_instructions"] =
        "Reply using the channels tool with action=reply; post_id is provided "
        "by the reply target and filled automatically. Text replies are NOT "
        "posted. Not every post needs a comment.";
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    std::string metadata = Json::writeString(wb, meta);

    ALOG_INFO("vk", "dispatching wall post from " << displayName
              << " for " << rt->channel_name);

    // Session = the post conversation (routing key == session key here).
    Dispatch("post:" + postId, prompt, "wall", metadata,
             "wall:" + rt->channel_name + ":post:" + postId);
}

void VkAdapter::ProcessWallReplyNew(const std::string& objectJson) {
    auto* rt = m_runtime.get();
    auto obj = ParseJson(objectJson);
    if (obj.isNull()) return;

    std::string postId = std::to_string(GetInt(obj, "post_id"));
    std::string fromId = std::to_string(GetInt(obj, "from_id"));
    std::string text = GetString(obj, "text");
    std::string replyToComment = std::to_string(GetInt(obj, "reply_to_comment"));

    int64_t groupId = 0;
    try { groupId = std::stoll(rt->group_id); } catch (...) {}
    int64_t fromIdVal = std::stoll(fromId);
    if (fromIdVal == -groupId || fromIdVal == groupId) {
        // Community-authored comment (admin "as community" or bot output):
        // anti-loop skip, but visible at debug level.
        ALOG_DEBUG("vk", "wall comment from the community itself (from_id="
                  << fromId << ") — skipping");
        return;
    }

    int64_t replyIdInt = GetInt(obj, "id");
    if (!rt->RememberEvent(replyIdInt)) return;

    auto names = ResolveUsers({fromId});
    std::string displayName = names.count(fromId) ? names[fromId] : fromId;

    // Fetch the post text for conversational context — mini-hydration so
    // the session opens knowing what thread it answers (#47-flavored).
    // Non-fatal: best effort.
    std::string postText;
    {
        std::string token = GetString(rt->config, "access_token");
        if (!token.empty() && !rt->group_id.empty()) {
            HttpClient::Request pr;
            pr.url = "https://api.vk.ru/method/wall.getById?"
                     "posts=-" + rt->group_id + "_" + postId +
                     "&access_token=" + token + "&v=5.131";
            pr.timeout_seconds = 10;
            auto pres = m_ctx.httpClient.Execute(pr);
            if (pres.status_code == 200) {
                auto pd = ParseJson(pres.body);
                if (pd.isMember("response") && pd["response"].isArray()
                    && pd["response"].size() > 0) {
                    postText = GetString(pd["response"][0], "text");
                }
            }
        }
        if (postText.size() > 400)
            postText = postText.substr(0, 400) + "…";
    }

    std::string prompt = "VK comment from " + displayName + " in thread:\n" + text;
    if (!postText.empty())
        prompt += "\n\n--- On post ---\n" + postText;

    Json::Value meta;
    meta["message_type"] = "wall";
    Json::Value origin;
    origin["user"] = displayName;
    origin["user_id"] = fromId;
    origin["channel"] = "thread";
    meta["origin"] = origin;
    meta["post_id"] = postId;
    if (!replyToComment.empty() && replyToComment != "0")
        meta["reply_parent_id"] = replyToComment; // comment being answered
    meta["source_message_id"] = std::to_string(replyIdInt);
    meta["reply_instructions"] =
        "Reply using the channels tool with action=reply; post_id (and the "
        "comment being answered) are provided by the reply target and filled "
        "automatically. Text replies are NOT posted. Not every comment needs "
        "a reply.";
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    std::string metadata = Json::writeString(wb, meta);

    ALOG_INFO("vk", "dispatching wall comment from " << displayName
              << " on post " << postId << " for " << rt->channel_name);

    // Routing key keeps the comment target (the reply answers a specific
    // comment); the SESSION is post-rooted — one session per wall-post
    // conversation; comments and sub-threads all meet there (Aug 31).
    std::string routingKey = "post:" + postId;
    if (!replyToComment.empty() && replyToComment != "0") {
        routingKey = "post:" + postId + ":comment:" + replyToComment;
    }

    Dispatch(routingKey, prompt, "wall", metadata,
             "wall:" + rt->channel_name + ":post:" + postId);
}

bool VkAdapter::FetchLongPollServer() {
    auto* rt = m_runtime.get();
    std::string token = GetString(rt->config, "access_token");
    std::string url = "https://api.vk.ru/method/groups.getLongPollServer?"
        "group_id=" + rt->group_id +
        "&v=5.131&access_token=" + token;

    HttpClient::Request req;
    req.url = url;
    req.timeout_seconds = 10;

    auto resp = m_ctx.httpClient.Execute(req);
    if (resp.status_code != 200) return false;

    auto json = ParseJson(resp.body);
    if (json.isMember("error")) return false;

    auto& response = json["response"];
    rt->lp_server = GetString(response, "server");
    rt->lp_key = GetString(response, "key");
    rt->lp_ts = GetString(response, "ts");

    if (rt->lp_server.empty() || rt->lp_key.empty()) return false;

    if (m_ctx.configStore) {
        m_ctx.configStore->Set("", "channel." + rt->channel_name + ".polling.server", rt->lp_server);
        m_ctx.configStore->Set("", "channel." + rt->channel_name + ".polling.key", rt->lp_key);
        m_ctx.configStore->Set("", "channel." + rt->channel_name + ".polling.ts", rt->lp_ts);
    }

    return true;
}

std::string VkAdapter::BuildLongPollUrl() const {
    auto* rt = m_runtime.get();
    return rt->lp_server + "?act=a_check&key=" + rt->lp_key + "&ts=" + rt->lp_ts
         + "&version=3";
}

std::unordered_map<std::string, std::string>
VkAdapter::ResolveUsers(const std::vector<std::string>& userIds) {
    auto* rt = m_runtime.get();
    std::unordered_map<std::string, std::string> result;
    if (userIds.empty()) return result;

    std::string token = GetString(rt->config, "access_token");
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

    auto resp = m_ctx.httpClient.Execute(req);
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

std::string VkAdapter::FetchChatHistory(const std::string& peerId, int count) {
    auto* rt = m_runtime.get();
    std::string token = GetString(rt->config, "access_token");
    std::string url = "https://api.vk.ru/method/messages.getHistory?"
        "peer_id=" + peerId +
        "&count=" + std::to_string(count) +
        "&v=5.131&access_token=" + token;

    HttpClient::Request req;
    req.url = url;
    req.timeout_seconds = 10;

    auto resp = m_ctx.httpClient.Execute(req);
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

    auto names = ResolveUsers(fromIds);
    std::string botName = "Bot";
    if (!rt->group_id.empty()) botName = "Bot (" + rt->group_id + ")";

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

void VkAdapter::SendReply(const ChannelReplyTarget& target, const std::string& text) {
    auto* rt = m_runtime.get();
    std::string token = GetString(rt->config, "access_token");
    if (token.empty()) return;

    std::string groupId = GetString(rt->config, "group_id");

    if (target.type == ChannelReplyTarget::Chat) {
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

        auto resp = m_ctx.httpClient.Execute(req);
        ALOG_DEBUG("vk", "Chat reply: " << resp.status_code
                  << " body=" << resp.body.substr(0, 200));
    } else if (target.type == ChannelReplyTarget::Wall) {
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

        auto resp = m_ctx.httpClient.Execute(req);
        ALOG_DEBUG("vk", "Wall reply: " << resp.status_code
                  << " body=" << resp.body.substr(0, 200));
    }
}

} // namespace animus::kernel

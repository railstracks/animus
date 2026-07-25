#include "animus_kernel/ChannelContext.h"

#include "animus_kernel/AgentConfigStore.h"
#include "animus_kernel/ChannelManager.h"

namespace animus::kernel {

void ChannelDispatch::Dispatch(ChannelContext& ctx,
                                PollerRuntime* state,
                                const std::string& routingKey,
                                const std::string& message,
                                const std::string& sessionType) {
    bool isPeer = (routingKey.size() > 5 && routingKey.substr(0, 5) == "peer:");
    bool isPost = (routingKey.size() > 5 && routingKey.substr(0, 5) == "post:");

    std::string routingValue = routingKey.substr(5);

    auto entry = ctx.router.Lookup(state->channel_name, routingKey);

    std::string sessionKey;
    if (entry) {
        sessionKey = entry->session_key;
    } else {
        sessionKey = sessionType + ":" + state->channel_name + ":" + routingValue;
        ctx.router.Register(state->channel_name, routingKey, sessionKey, state->agent_id);
    }

    ChannelReplyTarget replyTarget;
    replyTarget.channel_name = state->channel_name;
    replyTarget.channel_type = state->channel_type;

    bool isThread = (routingKey.size() > 7 && routingKey.substr(0, 7) == "thread:");

    if (isPeer) {
        replyTarget.type = ChannelReplyTarget::Chat;
        replyTarget.peer_id = routingValue;
    } else if (isPost) {
        replyTarget.type = ChannelReplyTarget::Wall;
        size_t commentPos = routingValue.find(":comment:");
        if (commentPos != std::string::npos) {
            replyTarget.post_id = routingValue.substr(0, commentPos);
            replyTarget.reply_to_comment = routingValue.substr(commentPos + 9);
        } else {
            replyTarget.post_id = routingValue;
        }
        replyTarget.group_id = state->group_id;
    } else if (isThread) {
        replyTarget.type = ChannelReplyTarget::Chat;
        replyTarget.email_thread_id = routingValue;
        auto& cfg = state->config;
        if (cfg.isMember("inbox_id") && cfg["inbox_id"].isString())
            replyTarget.email_inbox_id = cfg["inbox_id"].asString();
    }

    ctx.dispatch(state->agent_id, sessionKey, message, sessionType, replyTarget);
}

void ChannelDispatch::Log(ChannelContext& ctx,
                           PollerRuntime* state,
                           const std::string& routingKey,
                           const std::string& message,
                           const std::string& sessionType) {
    std::string routingValue = routingKey.substr(5);

    auto entry = ctx.router.Lookup(state->channel_name, routingKey);

    std::string sessionKey;
    if (entry) {
        sessionKey = entry->session_key;
    } else {
        sessionKey = sessionType + ":" + state->channel_name + ":" + routingValue;
        ctx.router.Register(state->channel_name, routingKey, sessionKey, state->agent_id);
    }

    ctx.logCallback(state->agent_id, sessionKey, message, sessionType);
}

} // namespace animus::kernel

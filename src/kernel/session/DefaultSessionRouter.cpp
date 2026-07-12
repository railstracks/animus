#include "animus_kernel/DefaultSessionRouter.h"

#include <unordered_set>
#include <utility>

namespace animus::kernel {

static std::string GetMeta(const IncomingEvent& e, const char* key) {
    auto it = e.metadata.find(key);
    if (it == e.metadata.end()) {
        return {};
    }
    return it->second;
}

DefaultSessionRouter::DefaultSessionRouter(std::vector<SessionRoutingRule> rules)
    : m_rules(std::move(rules)) {
}

SessionRoutingResult DefaultSessionRouter::Route(const IncomingEvent& event) {
    SessionRoutingResult r{};

    r.primary.connector = event.source.empty() ? std::string("unknown") : event.source;

    // Try the most likely keys. (We will refine once we implement real connectors.)
    r.primary.conversation_id = GetMeta(event, "conversation_id");
    if (r.primary.conversation_id.empty()) {
        r.primary.conversation_id = GetMeta(event, "channel_id");
    }
    if (r.primary.conversation_id.empty()) {
        r.primary.conversation_id = GetMeta(event, "chat_id");
    }
    if (r.primary.conversation_id.empty()) {
        r.primary.conversation_id = "global";
    }

    r.primary.thread_id = GetMeta(event, "thread_id");
    if (r.primary.thread_id.empty()) {
        r.primary.thread_id = GetMeta(event, "thread_ts");
    }

    std::unordered_set<std::string> seenContextKeys;
    seenContextKeys.insert(r.primary.ToString());

    for (const auto& rule : m_rules) {
        if (!rule.Matches(event)) {
            continue;
        }

        SessionKey contextKey = rule.ResolveContext(event);
        const std::string serialized = contextKey.ToString();
        if (seenContextKeys.insert(serialized).second) {
            r.context.push_back(std::move(contextKey));
        }
    }

    return r;
}

} // namespace animus::kernel

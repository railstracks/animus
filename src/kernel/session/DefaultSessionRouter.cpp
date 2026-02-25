#include "animus_kernel/DefaultSessionRouter.h"

namespace animus::kernel {

static std::string GetMeta(const IncomingEvent& e, const char* key) {
    auto it = e.metadata.find(key);
    if (it == e.metadata.end()) {
        return {};
    }
    return it->second;
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

    // Session routing for additional context sessions will be implemented later.
    // For now: no additional session contexts.
    return r;
}

} // namespace animus::kernel

#pragma once

#include <string>

namespace animus::kernel {

// ============================================================================
// ResolveDispatchAgentBinding — #268 adopt-on-dispatch decision (pure).
//
// Channel dispatch carries the agent that owns the channel (#262). Legacy
// sessions created before that may sit on the 'default' sentinel — the
// canonical *unbound* id and the store's partition key, NOT an ownership
// claim. The old adoption gates only fired on empty bindings (or empty
// history), so a sentinel-bound session with turns never re-bound and
// stayed invisible to the agent that owns the channel: history by numeric
// id hit the ownership wall, key/search under the real agent came up
// empty (live case: session #2450, channel:irc:irc:dm:Petrel, 35 turns).
//
// Decision table:
//   dispatchAgentId empty                          -> keep current (nothing to adopt)
//   current empty                                  -> dispatchAgentId (unbound follows the dispatcher; pre-existing behavior)
//   current == "default", dispatcher configured    -> dispatchAgentId (#268: sentinel adoption)
//   current == "default", dispatcher NOT configured-> keep "default" (an unknown agent id must not hijack history)
//   otherwise (already bound to a real agent)      -> keep current (cross-agent isolation: never re-bind)
// ============================================================================

inline std::string ResolveDispatchAgentBinding(
    const std::string& currentAgentId,
    const std::string& dispatchAgentId,
    bool dispatchAgentConfigured) {
    if (dispatchAgentId.empty()) return currentAgentId;
    if (currentAgentId.empty()) return dispatchAgentId;
    if (currentAgentId == "default" && dispatchAgentConfigured) {
        return dispatchAgentId;
    }
    return currentAgentId;
}

} // namespace animus::kernel

#pragma once

#include <string>
#include <unordered_map>

namespace animus::kernel {

// IncomingEvent is a normalized stimulus that can activate an agent.
//
// This is kernel-internal (not part of the module ABI).
struct IncomingEvent {
    // Logical source / connector id, e.g. "slack", "webhook", "cli".
    std::string source;

    // Connector-specific metadata used for routing (channel id, thread id, user id, …).
    std::unordered_map<std::string, std::string> metadata;

    // Optional textual payload (message body, webhook json-as-text, …)
    std::string text;
};

} // namespace animus::kernel

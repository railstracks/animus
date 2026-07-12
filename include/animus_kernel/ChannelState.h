#pragma once

#include <cstdint>
#include <string>

#include <json/value.h>

namespace animus::kernel {

struct ChannelState {
    std::string name;           // Unique channel name (e.g. "irc-libera", "telegram-main")
    std::string type;           // Connector type: "irc", "telegram", "vk", "bluesky", "mastodon"
    bool enabled{true};
    bool connected{false};
    std::uint64_t lastEventUnixMs{0};
    Json::Value config{Json::objectValue};  // Type-specific config blob
};

} // namespace animus::kernel

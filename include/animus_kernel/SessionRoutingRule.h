#pragma once

#include <string>
#include <vector>

#include "animus_kernel/IncomingEvent.h"
#include "animus_kernel/Session.h"

namespace animus::kernel {

enum class SessionRoutingMatchType {
    MetadataPresent,
    MetadataEquals,
    MetadataStartsWith
};

struct SessionRoutingCondition {
    SessionRoutingMatchType type{SessionRoutingMatchType::MetadataPresent};
    std::string key;
    std::string value;

    bool Matches(const IncomingEvent& event) const;
};

struct SessionKeyTemplate {
    std::string connector;
    std::string conversation_id;
    std::string thread_id;

    SessionKey Resolve(const IncomingEvent& event) const;
};

struct SessionRoutingRule {
    std::vector<SessionRoutingCondition> conditions;
    SessionKeyTemplate context;

    bool Matches(const IncomingEvent& event) const;
    SessionKey ResolveContext(const IncomingEvent& event) const;
};

} // namespace animus::kernel

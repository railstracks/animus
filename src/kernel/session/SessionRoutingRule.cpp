#include "animus_kernel/SessionRoutingRule.h"

namespace animus::kernel {

namespace {

std::string GetMetadataValue(const IncomingEvent& event, const std::string& key) {
    auto it = event.metadata.find(key);
    if (it == event.metadata.end()) {
        return {};
    }

    return it->second;
}

std::string RenderTemplateValue(const std::string& templ, const IncomingEvent& event) {
    std::string rendered;
    rendered.reserve(templ.size());

    std::size_t cursor = 0;
    while (cursor < templ.size()) {
        const std::size_t open = templ.find('{', cursor);
        if (open == std::string::npos) {
            rendered.append(templ, cursor, templ.size() - cursor);
            break;
        }

        rendered.append(templ, cursor, open - cursor);

        const std::size_t close = templ.find('}', open + 1);
        if (close == std::string::npos) {
            rendered.append(templ, open, templ.size() - open);
            break;
        }

        const std::string token = templ.substr(open + 1, close - open - 1);
        if (token == "source") {
            rendered += event.source;
        } else if (token.rfind("meta:", 0) == 0) {
            rendered += GetMetadataValue(event, token.substr(5));
        } else {
            rendered += templ.substr(open, close - open + 1);
        }

        cursor = close + 1;
    }

    return rendered;
}

} // namespace

bool SessionRoutingCondition::Matches(const IncomingEvent& event) const {
    const std::string metadataValue = GetMetadataValue(event, key);

    switch (type) {
    case SessionRoutingMatchType::MetadataPresent:
        return !metadataValue.empty();
    case SessionRoutingMatchType::MetadataEquals:
        return metadataValue == value;
    case SessionRoutingMatchType::MetadataStartsWith:
        return metadataValue.rfind(value, 0) == 0;
    }

    return false;
}

SessionKey SessionKeyTemplate::Resolve(const IncomingEvent& event) const {
    return SessionKey{
        RenderTemplateValue(connector, event),
        RenderTemplateValue(conversation_id, event),
        RenderTemplateValue(thread_id, event),
    };
}

bool SessionRoutingRule::Matches(const IncomingEvent& event) const {
    for (const auto& condition : conditions) {
        if (!condition.Matches(event)) {
            return false;
        }
    }

    return true;
}

SessionKey SessionRoutingRule::ResolveContext(const IncomingEvent& event) const {
    return context.Resolve(event);
}

} // namespace animus::kernel

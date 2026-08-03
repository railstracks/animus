#include "animus_kernel/ToolSchemaService.h"

#include <algorithm>
#include <iostream>
#include <set>
#include <sstream>

#include <json/json.h>
#include <json/writer.h>

#include "animus_kernel/tools/ChannelsTool.h"

namespace animus::kernel {

ToolSchemaService::ToolSchemaService(ToolRegistry& tools, AgentStore* agentStore)
    : m_tools(tools)
    , m_agentStore(agentStore) {
}

llm::LLMToolDef ToolSchemaService::Convert(const ToolDefinition& def) {
    llm::LLMToolDef ltd;
    ltd.type = "function";
    ltd.name = def.name;
    ltd.description = def.description;

    Json::Value schema(Json::objectValue);
    schema["type"] = "object";
    Json::Value properties(Json::objectValue);
    Json::Value required(Json::arrayValue);

    for (const auto& param : def.parameters) {
        Json::Value prop(Json::objectValue);
        prop["type"] = param.type;
        prop["description"] = param.description;
        if (!param.enum_values.empty()) {
            Json::Value en(Json::arrayValue);
            for (const auto& e : param.enum_values) {
                en.append(e);
            }
            prop["enum"] = en;
        }
        if (!param.properties.empty()) {
            Json::Value nestedProps(Json::objectValue);
            for (const auto& np : param.properties) {
                Json::Value npVal(Json::objectValue);
                npVal["type"] = np.type;
                npVal["description"] = np.description;
                nestedProps[np.name] = npVal;
            }
            prop["properties"] = nestedProps;
        }
        properties[param.name] = prop;
        if (param.required) {
            required.append(param.name);
        }
    }

    schema["properties"] = properties;
    if (required.size() > 0) {
        schema["required"] = required;
    }

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    ltd.parameters = Json::writeString(builder, schema);

    return ltd;
}

std::vector<llm::LLMToolDef> ToolSchemaService::GetAll() const {
    std::vector<llm::LLMToolDef> defs;
    for (const auto& def : m_tools.GetAllDefinitions()) {
        defs.push_back(Convert(def));
    }
    return defs;
}

std::vector<llm::LLMToolDef> ToolSchemaService::GetForAgent(const std::string& agentId) const {
    if (!m_agentStore || agentId.empty()) {
        return GetAll();
    }

    auto agent = m_agentStore->GetById(agentId);
    if (!agent || agent->enabled_tools.empty()) {
        return GetAll();
    }

    // Set agent context on ChannelsTool for per-agent schema filtering
    auto* channelsHandler = m_tools.Find("channels");
    if (channelsHandler) {
        auto* channelsTool = dynamic_cast<ChannelsTool*>(channelsHandler);
        if (channelsTool) {
            channelsTool->SetCurrentAgentId(agentId);
        }
    }

    std::vector<llm::LLMToolDef> defs;
    for (const auto& def : m_tools.GetAllDefinitions()) {
        if (std::find(agent->enabled_tools.begin(), agent->enabled_tools.end(), def.name)
            != agent->enabled_tools.end()) {
            defs.push_back(Convert(def));
        }
    }

    // Clear agent context to avoid state leakage
    if (channelsHandler) {
        auto* channelsTool = dynamic_cast<ChannelsTool*>(channelsHandler);
        if (channelsTool) {
            channelsTool->SetCurrentAgentId("");
        }
    }

    return defs;
}

std::vector<llm::LLMToolDef> ToolSchemaService::GetForSession(
    const std::string& agentId,
    const std::string& sessionType) const {

    // Consolidation sessions: bypass agent whitelist, use dedicated toolset
    if (sessionType == "consolidation") {
        static const std::set<std::string> kConsolidationSafe = {
            "diary", "memory", "sessions"
        };

        std::vector<llm::LLMToolDef> defs;
        for (const auto& def : m_tools.GetAllDefinitions()) {
            auto* handler = m_tools.Find(def.name);
            if (!handler) continue;
            const auto& td = handler->GetDefinition();

            if (!td.session_types.empty()) {
                for (const auto& st : td.session_types) {
                    if (st == "consolidation") {
                        defs.push_back(Convert(def));
                        break;
                    }
                }
            } else {
                if (kConsolidationSafe.count(def.name)) {
                    defs.push_back(Convert(def));
                }
            }
        }
        return defs;
    }

    // Default path: agent whitelist + session type filter
    auto agentDefs = GetForAgent(agentId);

    std::vector<llm::LLMToolDef> defs;
    for (const auto& def : agentDefs) {
        auto* handler = m_tools.Find(def.name);
        if (!handler) continue;
        const auto& td = handler->GetDefinition();
        if (td.session_types.empty()) {
            defs.push_back(def);
        } else {
            for (const auto& st : td.session_types) {
                // "default" matches any non-consolidation session type
                // (gallivanting, chat, scheduled, etc.) — not just empty sessionType.
                if (st == sessionType ||
                    (st == "default" && sessionType != "consolidation")) {
                    defs.push_back(def);
                    break;
                }
            }
        }
    }
    return defs;
}

} // namespace animus::kernel

#include "animus_kernel/tools/NodeTool.h"
#include "animus_kernel/NodeManager.h"

#include <chrono>
#include <iostream>
#include <json/json.h>

namespace animus::kernel {

NodeTool::NodeTool(NodeManager* nodeManager)
    : m_nodeManager(nodeManager) {}

ToolDefinition NodeTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "node";
    def.description = "Discover and inspect external nodes available for remote tool execution. "
                      "Use action 'list' to see connected nodes, 'info' for details on a specific node. "
                      "To execute tools on a node, add \"__node\": \"<name>\" to any tool call.";
    def.resultMode = ToolResultMode::deliver_to_model;
    def.parameters.push_back({"action", "string",
        "Action: 'list' (show connected nodes) or 'info' (show node details)", true});
    def.parameters.push_back({"name", "string",
        "Node name (required for 'info' action)", false});
    return def;
}

ToolResult NodeTool::Execute(const ToolCall& call) {
    ToolResult result;
    result.call_id = call.id;

    if (!m_nodeManager) {
        result.success = false;
        result.error = "node manager not available";
        return result;
    }

    // Parse action
    Json::Value args;
    Json::CharReaderBuilder builder;
    std::string parseErr;
    auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
    if (!reader->parse(call.arguments.c_str(),
                       call.arguments.c_str() + call.arguments.size(),
                       &args, &parseErr)) {
        result.success = false;
        result.error = "failed to parse arguments: " + parseErr;
        return result;
    }

    std::string agentId = args.get("__agent_id", "").asString();
    std::string action = args.get("action", "").asString();

    if (action == "list") {
        return HandleList(agentId);
    } else if (action == "info") {
        return HandleInfo(agentId, call.arguments);
    } else {
        result.success = false;
        result.error = "unknown action: " + action + ". Available: list, info";
        return result;
    }
}

ToolResult NodeTool::HandleList(const std::string& agentId) {
    ToolResult result;
    result.call_id = "";

    auto nodes = m_nodeManager->ListNodesForAgent(agentId);

    Json::Value root(Json::objectValue);
    root["total"] = static_cast<Json::UInt>(nodes.size());

    Json::Value nodesArr(Json::arrayValue);
    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    for (const auto& n : nodes) {
        Json::Value nodeObj(Json::objectValue);
        nodeObj["name"] = n.name;
        nodeObj["status"] = n.connected ? "connected" : "disconnected";
        nodeObj["hostname"] = n.hostname;
        nodeObj["os"] = n.os_info;
        nodeObj["uptime_seconds"] = static_cast<Json::UInt64>(
            n.connected ? (nowMs - n.connected_at_unix_ms) / 1000 : 0);

        Json::Value toolsArr(Json::arrayValue);
        for (const auto& t : n.tools) toolsArr.append(t);
        nodeObj["tools"] = toolsArr;

        nodesArr.append(nodeObj);
    }
    root["nodes"] = nodesArr;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    result.success = true;
    result.output = Json::writeString(wb, root);
    return result;
}

ToolResult NodeTool::HandleInfo(const std::string& agentId, const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";

    Json::Value args;
    Json::CharReaderBuilder builder;
    std::string parseErr;
    auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
    reader->parse(rawArgs.c_str(), rawArgs.c_str() + rawArgs.size(), &args, &parseErr);

    std::string name = args.get("name", "").asString();
    if (name.empty()) {
        result.success = false;
        result.error = "required: name";
        return result;
    }

    auto info = m_nodeManager->GetNode(name);
    if (!info) {
        result.success = false;
        result.error = "node not found: " + name;
        return result;
    }

    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    Json::Value root(Json::objectValue);
    root["name"] = info->name;
    root["status"] = info->connected ? "connected" : "disconnected";
    root["hostname"] = info->hostname;
    root["os"] = info->os_info;
    root["connected_at"] = static_cast<Json::UInt64>(info->connected_at_unix_ms);
    root["uptime_seconds"] = static_cast<Json::UInt64>(
        info->connected ? (nowMs - info->connected_at_unix_ms) / 1000 : 0);
    root["last_seen"] = static_cast<Json::UInt64>(info->last_seen_unix_ms);

    Json::Value toolsArr(Json::arrayValue);
    for (const auto& t : info->tools) toolsArr.append(t);
    root["tools"] = toolsArr;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    result.success = true;
    result.output = Json::writeString(wb, root);
    return result;
}

} // namespace animus::kernel

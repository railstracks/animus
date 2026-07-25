#include "animus_kernel/ToolExecutionService.h"

#include <iostream>
#include <memory>
#include <sstream>

#include <json/json.h>
#include <json/reader.h>
#include <json/writer.h>

#include "animus_kernel/SessionManager.h"

namespace animus::kernel {

namespace {

bool ParseJsonObject(const std::string& text, Json::Value* out) {
    if (!out) return false;
    Json::CharReaderBuilder builder;
    std::istringstream stream(text);
    std::string errors;
    return Json::parseFromStream(builder, stream, out, &errors) && out->isObject();
}

std::string WriteCompactJson(const Json::Value& val) {
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, val);
}

// --- Tool config injection (file policy, stored_links, rss) ---

std::string InjectFilePolicy(const std::string& argumentsJson, const std::string& toolConfigsJson) {
    Json::Value toolConfigs(Json::objectValue);
    if (!ParseJsonObject(toolConfigsJson, &toolConfigs)) return argumentsJson;
    if (!toolConfigs.isMember("file") || !toolConfigs["file"].isObject()) return argumentsJson;

    Json::Value args(Json::objectValue);
    if (!ParseJsonObject(argumentsJson, &args)) return argumentsJson;

    const Json::Value& filePolicy = toolConfigs["file"];
    Json::Value policy(Json::objectValue);
    if (filePolicy.isMember("restrict_to_workspace")) {
        policy["restrict_to_workspace"] = filePolicy["restrict_to_workspace"];
    }
    if (filePolicy.isMember("workspace_root")) {
        policy["workspace_root"] = filePolicy["workspace_root"];
    }
    if (filePolicy.isMember("path_allowlist")) {
        policy["path_allowlist"] = filePolicy["path_allowlist"];
    }
    if (filePolicy.isMember("path_denylist")) {
        policy["path_denylist"] = filePolicy["path_denylist"];
    }
    if (policy.empty()) return argumentsJson;
    args["__policy"] = policy;

    return WriteCompactJson(args);
}

std::string InjectToolConfig(const std::string& toolName,
                             const std::string& argumentsJson,
                             const std::string& toolConfigsJson) {
    if (toolName == "file") {
        return InjectFilePolicy(argumentsJson, toolConfigsJson);
    }

    Json::Value toolConfigs(Json::objectValue);
    if (!ParseJsonObject(toolConfigsJson, &toolConfigs)) return argumentsJson;

    std::string configKey;
    std::string injectKey;
    if (toolName == "stored_links") {
        configKey = "stored_links";
        injectKey = "links";
    } else if (toolName == "rss") {
        configKey = "rss_feeds";
        injectKey = "feeds";
    } else {
        return argumentsJson;
    }

    if (!toolConfigs.isMember(configKey) || !toolConfigs[configKey].isArray()) return argumentsJson;

    Json::Value args(Json::objectValue);
    if (!ParseJsonObject(argumentsJson, &args)) return argumentsJson;

    Json::Value config(Json::objectValue);
    config[injectKey] = toolConfigs[configKey];
    args["__config"] = config;

    return WriteCompactJson(args);
}

} // namespace

// ============================================================================
// ToolExecutionService
// ============================================================================

ToolExecutionService::ToolExecutionService(
    ToolRegistry& tools, AgentStore* agentStore, NodeManager* nodeManager)
    : m_tools(tools)
    , m_agentStore(agentStore)
    , m_nodeManager(nodeManager) {
}

std::string ToolExecutionService::InjectContext(
    const std::string& toolName,
    const std::string& argumentsJson,
    const ToolExecutionContext& ctx) {

    // Step 1: Inject per-agent tool configs (file policy, stored_links, rss)
    std::string result = argumentsJson;
    if ((toolName == "file" || toolName == "stored_links" || toolName == "rss") &&
        !ctx.toolConfigs.empty()) {
        result = InjectToolConfig(toolName, result, ctx.toolConfigs);
    }

    // Step 2: Inject session context (__session_key, __agent_id)
    Json::Value args;
    if (ParseJsonObject(result, &args)) {
        args["__session_key"] = ctx.sessionKey;
        args["__agent_id"] = ctx.agentId;
        result = WriteCompactJson(args);
    }

    return result;
}

ToolResult ToolExecutionService::Execute(
    const llm::LLMToolCall& call,
    const ToolExecutionContext& ctx,
    ToolRouteResult* routeResult) const {

    // Build the ToolCall with injected context
    ToolCall tc;
    tc.id = call.id;
    tc.name = call.name;
    tc.arguments = InjectContext(call.name, call.arguments, ctx);

    // Check for __node parameter — remote node forwarding
    std::string nodeName;
    {
        Json::Value argsJson;
        Json::CharReaderBuilder rb;
        std::string parseErr;
        auto reader = std::unique_ptr<Json::CharReader>(rb.newCharReader());
        if (reader->parse(tc.arguments.c_str(),
                          tc.arguments.c_str() + tc.arguments.size(),
                          &argsJson, &parseErr)) {
            nodeName = argsJson.get("__node", "").asString();
        }
    }

    ToolResult toolResult;

    if (!nodeName.empty() && m_nodeManager) {
        // Forward to remote node
        std::cerr << "[tool-exec] Forwarding to node: " << nodeName << std::endl;
        toolResult = m_nodeManager->ExecuteOnNode(nodeName, tc);
        std::cerr << "[tool-exec] Node result: success=" << toolResult.success
                  << " output_len=" << toolResult.output.size() << std::endl;

        if (routeResult) {
            *routeResult = ToolRouteResult::deliver_to_model;
        }
    } else {
        auto* handler = m_tools.Find(call.name);
        if (handler) {
            try {
                toolResult = handler->Execute(tc);
            } catch (const std::exception& e) {
                std::cerr << "[tool-exec] Tool execution threw: " << e.what() << std::endl;
                toolResult.call_id = call.id;
                toolResult.success = false;
                toolResult.error = std::string("Tool execution failed: ") + e.what();
            }
            std::cerr << "[tool-exec] Tool result: success=" << toolResult.success
                      << " output_len=" << toolResult.output.size()
                      << " error=" << toolResult.error << std::endl;

            // Determine result routing from handler
            if (routeResult) {
                ToolResultMode mode = handler->GetResultMode();
                switch (mode) {
                    case ToolResultMode::stream_to_user:
                        *routeResult = ToolRouteResult::stream_to_user;
                        break;
                    case ToolResultMode::both:
                        *routeResult = ToolRouteResult::both;
                        break;
                    default:
                        *routeResult = ToolRouteResult::deliver_to_model;
                        break;
                }
            }
        } else {
            toolResult.call_id = call.id;
            toolResult.success = false;
            toolResult.error = "unknown tool: " + call.name;
            if (routeResult) {
                *routeResult = ToolRouteResult::deliver_to_model;
            }
        }
    }

    return toolResult;
}

} // namespace animus::kernel

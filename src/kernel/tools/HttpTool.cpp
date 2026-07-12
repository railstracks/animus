#include "animus_kernel/tools/HttpTool.h"

#include <json/json.h>
#include <sstream>

namespace animus::kernel {

namespace {

Json::Value ParseArgs(const std::string& args) {
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::istringstream stream(args);
    std::string errors;
    if (!Json::parseFromStream(builder, stream, &root, &errors)) {
        return {};
    }
    return root;
}

std::string GetStringField(const Json::Value& args, const std::string& key,
                           const std::string& defaultVal = "") {
    if (args.isMember(key) && args[key].isString()) {
        return args[key].asString();
    }
    return defaultVal;
}

int GetIntField(const Json::Value& args, const std::string& key, int defaultVal = 0) {
    if (args.isMember(key) && args[key].isInt()) {
        return args[key].asInt();
    }
    return defaultVal;
}

} // anonymous namespace

// ============================================================================
// Construction
// ============================================================================

HttpTool::HttpTool(HttpClient& client)
    : m_client(client) {}

// ============================================================================
// Definition
// ============================================================================

ToolDefinition HttpTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "http";
    def.description =
        "Make an HTTP request. Use for API calls, webhooks, health checks, "
        "and any HTTP interaction that doesn't fit web_search or web_fetch. "
        "Returns {status_code, headers, body, elapsed_ms}.";
    def.resultMode = ToolResultMode::deliver_to_model;

    ToolParameter methodParam;
    methodParam.name = "method";
    methodParam.type = "string";
    methodParam.description = "HTTP method";
    methodParam.required = true;
    methodParam.enum_values = {"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD"};
    def.parameters.push_back(methodParam);

    ToolParameter urlParam;
    urlParam.name = "url";
    urlParam.type = "string";
    urlParam.description = "Request URL";
    urlParam.required = true;
    def.parameters.push_back(urlParam);

    ToolParameter headersParam;
    headersParam.name = "headers";
    headersParam.type = "object";
    headersParam.description = "Request headers as key-value pairs";
    headersParam.required = false;
    def.parameters.push_back(headersParam);

    ToolParameter bodyParam;
    bodyParam.name = "body";
    bodyParam.type = "string";
    bodyParam.description = "Request body (JSON string for JSON APIs, plain text otherwise)";
    bodyParam.required = false;
    def.parameters.push_back(bodyParam);

    ToolParameter timeoutParam;
    timeoutParam.name = "timeout";
    timeoutParam.type = "integer";
    timeoutParam.description = "Request timeout in seconds (default 30)";
    timeoutParam.required = false;
    def.parameters.push_back(timeoutParam);

    return def;
}

// ============================================================================
// Execute
// ============================================================================

ToolResult HttpTool::Execute(const ToolCall& call) {
    ToolResult result;
    result.call_id = call.id;

    auto args = ParseArgs(call.arguments);
    if (args.isNull()) {
        result.success = false;
        result.error = "Failed to parse tool arguments as JSON";
        return result;
    }

    std::string method = GetStringField(args, "method", "GET");
    std::string url = GetStringField(args, "url");
    std::string body = GetStringField(args, "body");
    int timeout = GetIntField(args, "timeout", 30);

    if (url.empty()) {
        result.success = false;
        result.error = "Missing required parameter: url";
        return result;
    }

    // Build request
    HttpClient::Request req;
    req.method = method;
    req.url = url;
    req.body = body;
    req.timeout_seconds = timeout;

    // Parse headers object
    if (args.isMember("headers") && args["headers"].isObject()) {
        const auto& hdrs = args["headers"];
        for (const auto& key : hdrs.getMemberNames()) {
            if (hdrs[key].isString()) {
                req.headers[key] = hdrs[key].asString();
            }
        }
    }

    // Set Content-Type for JSON bodies if not specified
    if (!body.empty() && req.headers.find("Content-Type") == req.headers.end()) {
        // Heuristic: if body starts with { or [, assume JSON
        std::string trimmed = body;
        while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\n'))
            trimmed.erase(trimmed.begin());
        if (trimmed.starts_with('{') || trimmed.starts_with('[')) {
            req.headers["Content-Type"] = "application/json";
        }
    }

    // Execute
    auto response = m_client.Execute(req);

    if (!response.error.empty()) {
        result.success = false;
        result.error = response.error;
        return result;
    }

    // Build structured output
    Json::Value output(Json::objectValue);
    output["status_code"] = response.status_code;
    output["elapsed_ms"] = static_cast<Json::UInt64>(response.elapsed_ms);

    // Headers as object
    Json::Value headers(Json::objectValue);
    for (const auto& [key, value] : response.headers) {
        headers[key] = value;
    }
    output["headers"] = headers;

    // Body
    output["body"] = response.body;

    Json::StreamWriterBuilder writer;
    result.output = Json::writeString(writer, output);
    result.success = (response.status_code >= 200 && response.status_code < 300);

    if (!result.success) {
        result.error = "HTTP " + std::to_string(response.status_code);
    }

    return result;
}

} // namespace animus::kernel
#include "animus_kernel/tools/StoredLinksTool.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include <json/json.h>

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

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

StoredLinksTool::StoredLinksTool(HttpClient& client, std::vector<StoredLink> links)
    : m_client(client), m_links(std::move(links)) {}

ToolDefinition StoredLinksTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "stored_links";
    def.description =
        "Fetch pre-configured HTTP links by identifier. "
        "Use \"list\" as the id to see all available stored links. "
        "Returns the raw response body with content-type information.";
    def.resultMode = ToolResultMode::deliver_to_model;
    def.session_types = {"default"};

    ToolParameter idParam;
    idParam.name = "id";
    idParam.type = "string";
    idParam.description = "Identifier of the stored link to fetch, or \"list\" to enumerate available links.";
    idParam.required = true;
    def.parameters.push_back(idParam);

    // Config parameters (per-agent, stored in tool_configs.stored_links)
    ToolParameter cfgLinks;
    cfgLinks.name = "links";
    cfgLinks.type = "array";
    cfgLinks.description = "Array of stored link objects: {id, label, url}. Per-agent override for system-wide defaults.";
    cfgLinks.required = false;
    def.config_parameters.push_back(cfgLinks);

    return def;
}

std::string StoredLinksTool::FormatList(const std::vector<StoredLink>& links) const {
    if (links.empty()) {
        return "No stored links configured.";
    }

    Json::Value root(Json::arrayValue);
    for (const auto& link : links) {
        Json::Value entry(Json::objectValue);
        entry["id"] = link.id;
        entry["label"] = link.label;
        root.append(entry);
    }

    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "  ";
    return Json::writeString(wb, root);
}

ToolResult StoredLinksTool::Execute(const ToolCall& call) {
    ToolResult result;
    result.call_id = call.id;

    // Determine which links to use: per-agent injected config overrides constructor defaults
    std::vector<StoredLink> activeLinks = m_links;
    const auto args = ParseArgs(call.arguments);

    if (args.isMember("__config") && args["__config"].isObject() &&
        args["__config"].isMember("links") && args["__config"]["links"].isArray()) {
        activeLinks.clear();
        for (const auto& item : args["__config"]["links"]) {
            StoredLink link;
            link.id = item.get("id", "").asString();
            link.label = item.get("label", "").asString();
            link.url = item.get("url", "").asString();
            if (!link.id.empty() && !link.url.empty()) {
                activeLinks.push_back(std::move(link));
            }
        }
    }

    if (activeLinks.empty()) {
        result.success = false;
        result.error = "No stored links are configured for this agent. Add links in the tool configuration.";
        return result;
    }
    const std::string id = args.isMember("id") && args["id"].isString()
        ? args["id"].asString() : "";

    if (id.empty()) {
        result.success = false;
        result.error = "Missing required parameter: id";
        return result;
    }

    // List mode
    if (ToLower(id) == "list") {
        result.success = true;
        result.output = FormatList(activeLinks);
        return result;
    }

    // Find the stored link
    auto it = std::find_if(activeLinks.begin(), activeLinks.end(),
        [&](const StoredLink& l) { return l.id == id; });

    if (it == m_links.end()) {
        result.success = false;
        result.error = "No stored link with id \"" + id + "\". Use \"list\" to see available links.";
        return result;
    }

    // Fetch the URL
    HttpClient::Request req;
    req.url = it->url;
    req.method = "GET";
    req.timeout_seconds = 15;

    auto resp = m_client.Execute(req);
    if (resp.status_code == 0) {
        result.success = false;
        result.error = "HTTP request failed for stored link \"" + id + "\": " + it->url;
        return result;
    }

    if (resp.status_code >= 400) {
        result.success = false;
        result.error = "HTTP " + std::to_string(resp.status_code) + " from " + it->url;
        return result;
    }

    // Return the raw body with content-type metadata
    Json::Value output(Json::objectValue);
    output["id"] = id;
    output["label"] = it->label;
    output["url"] = it->url;
    output["status"] = resp.status_code;
    output["content_type"] = resp.content_type;
    output["body"] = resp.body;

    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    result.success = true;
    result.output = Json::writeString(wb, output);
    return result;
}

} // namespace animus::kernel
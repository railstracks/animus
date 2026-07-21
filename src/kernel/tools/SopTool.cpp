#include "animus_kernel/tools/SopTool.h"

#include <json/json.h>
#include <sstream>
#include <iostream>

namespace animus::kernel {

static Json::Value ParseArgs(const std::string& args) {
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::istringstream stream(args);
    std::string errors;
    Json::parseFromStream(builder, stream, &root, &errors);
    return root;
}

static std::string JsonToString(const Json::Value& v) {
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "  ";
    return Json::writeString(wb, v);
}

ToolDefinition SopTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "sop";
    def.description =
        "Browse and load Standard Operating Procedures (SOPs) — reusable "
        "workflow guides, setup procedures, and best-practice templates. "
        "Loading an SOP writes it to memory for persistent access and "
        "consolidation. Use 'list' to see available SOPs, 'search' to find "
        "by keyword, or 'load' to internalize a specific SOP.";

    // action parameter
    ToolParameter actionParam;
    actionParam.name = "action";
    actionParam.type = "string";
    actionParam.description = "Action to perform: list, search, or load";
    actionParam.required = true;
    actionParam.enum_values = {"list", "search", "load"};
    def.parameters.push_back(actionParam);

    // category (for list)
    ToolParameter catParam;
    catParam.name = "category";
    catParam.type = "string";
    catParam.description = "Filter by category (for 'list' action). Empty = all categories.";
    catParam.required = false;
    def.parameters.push_back(catParam);

    // query (for search)
    ToolParameter queryParam;
    queryParam.name = "query";
    queryParam.type = "string";
    queryParam.description = "Search keyword/phrase (for 'search' action)";
    queryParam.required = false;
    def.parameters.push_back(queryParam);

    // name (for load)
    ToolParameter nameParam;
    nameParam.name = "name";
    nameParam.type = "string";
    nameParam.description = "SOP identifier/slug to load (for 'load' action)";
    nameParam.required = false;
    def.parameters.push_back(nameParam);

    // page
    ToolParameter pageParam;
    pageParam.name = "page";
    pageParam.type = "integer";
    pageParam.description = "Page number (default 1)";
    pageParam.required = false;
    def.parameters.push_back(pageParam);

    // per_page
    ToolParameter perPageParam;
    perPageParam.name = "per_page";
    perPageParam.type = "integer";
    perPageParam.description = "Items per page (default 20)";
    perPageParam.required = false;
    def.parameters.push_back(perPageParam);

    return def;
}

ToolResult SopTool::Execute(const ToolCall& call) {
    auto args = ParseArgs(call.arguments);
    std::string action = args.get("action", "").asString();

    if (action == "list") return HandleList(call.arguments);
    if (action == "search") return HandleSearch(call.arguments);
    if (action == "load") return HandleLoad(call.arguments);

    ToolResult result;
    result.call_id = call.id;
    result.success = false;
    result.error = "Unknown action: '" + action + "'. Valid actions: list, search, load.";
    return result;
}

ToolResult SopTool::HandleList(const std::string& argsStr) {
    auto args = ParseArgs(argsStr);
    std::string category = args.get("category", "").asString();
    int page = args.get("page", 1).asInt();
    int perPage = args.get("per_page", 20).asInt();
    if (page < 1) page = 1;
    if (perPage < 1) perPage = 20;

    auto sops = m_store->List(category, page, perPage);
    size_t total = m_store->Count(category);

    Json::Value root;
    root["action"] = "list";
    root["page"] = page;
    root["per_page"] = perPage;
    root["total"] = static_cast<Json::UInt64>(total);
    root["total_pages"] = static_cast<Json::UInt64>((total + perPage - 1) / perPage);

    Json::Value items(Json::arrayValue);
    for (const auto& s : sops) {
        Json::Value item;
        item["name"] = s.name;
        item["title"] = s.title;
        item["category"] = s.category;
        item["version"] = s.version;
        item["description"] = s.description;
        items.append(item);
    }
    root["sops"] = items;

    ToolResult result;
    result.call_id = "";
    result.success = true;
    result.output = "Found " + std::to_string(total) + " SOP(s)";
    if (!category.empty()) result.output += " in category '" + category + "'";
    result.output += ":\n\n" + JsonToString(root);
    return result;
}

ToolResult SopTool::HandleSearch(const std::string& argsStr) {
    auto args = ParseArgs(argsStr);
    std::string query = args.get("query", "").asString();
    if (query.empty()) {
        ToolResult result;
        result.success = false;
        result.error = "Missing required parameter 'query' for search action.";
        return result;
    }
    int page = args.get("page", 1).asInt();
    int perPage = args.get("per_page", 20).asInt();
    if (page < 1) page = 1;
    if (perPage < 1) perPage = 20;

    auto sops = m_store->Search(query, page, perPage);

    Json::Value root;
    root["action"] = "search";
    root["query"] = query;
    root["page"] = page;
    root["per_page"] = perPage;
    root["total"] = static_cast<Json::UInt64>(sops.size());

    Json::Value items(Json::arrayValue);
    for (const auto& s : sops) {
        Json::Value item;
        item["name"] = s.name;
        item["title"] = s.title;
        item["category"] = s.category;
        item["version"] = s.version;
        item["description"] = s.description;
        items.append(item);
    }
    root["sops"] = items;

    ToolResult result;
    result.success = true;
    result.output = "Search for '" + query + "' returned " +
                    std::to_string(sops.size()) + " result(s):\n\n" + JsonToString(root);
    return result;
}

ToolResult SopTool::HandleLoad(const std::string& argsStr) {
    auto args = ParseArgs(argsStr);
    std::string name = args.get("name", "").asString();
    if (name.empty()) {
        ToolResult result;
        result.success = false;
        result.error = "Missing required parameter 'name' for load action.";
        return result;
    }

    auto sop = m_store->Get(name);
    if (!sop) {
        ToolResult result;
        result.success = false;
        result.error = "SOP not found: '" + name + "'. Use 'list' or 'search' to find available SOPs.";
        return result;
    }

    // Write to MemoryFiles for persistent access
    std::string memoryPath = "sop." + sop->meta.name;
    bool memoryWritten = false;

    if (m_memoryFiles) {
        try {
            memory::MemoryFile mf;
            mf.source_path = memoryPath;
            mf.file_type = memory::MemoryFileType::ExternalDoc;
            mf.content = sop->content;
            mf.content_mutable = true;
            mf.agent_id = m_agentId;
            mf.status = memory::MemoryFileStatus::Unprocessed;

            auto created = m_memoryFiles->CreateFile(mf);
            memoryWritten = true;
            memoryPath = "memoryFile." + memoryPath + " (id=" + std::to_string(created.id) + ")";

            std::cerr << "[sop] Loaded '" << sop->meta.name << "' into MemoryFile id="
                      << created.id << " for agent " << m_agentId << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[sop] Failed to write MemoryFile: " << e.what() << std::endl;
        }
    }

    Json::Value root;
    root["action"] = "load";
    root["name"] = sop->meta.name;
    root["title"] = sop->meta.title;
    root["version"] = sop->meta.version;
    root["memory_path"] = memoryPath;
    root["persisted"] = memoryWritten;
    root["content_length"] = static_cast<Json::UInt64>(sop->content.size());

    ToolResult result;
    result.success = true;
    result.output = "SOP '" + sop->meta.name + "' loaded successfully.\n" +
                    "Title: " + sop->meta.title + "\n" +
                    "Version: " + sop->meta.version + "\n" +
                    "Persisted to memory: " + (memoryWritten ? "yes" : "no (memory store unavailable)") + "\n" +
                    "The consolidation pipeline will process this SOP in the next intake cycle.";
    return result;
}

} // namespace animus::kernel

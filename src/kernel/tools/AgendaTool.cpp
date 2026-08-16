#include "animus_kernel/tools/AgendaTool.h"

#include <json/json.h>
#include <json/writer.h>
#include <sstream>

namespace animus::kernel {

namespace {

std::string GetStr(const Json::Value& v, const std::string& key, const std::string& def = "") {
    if (v.isMember(key) && v[key].isString()) return v[key].asString();
    return def;
}

int64_t GetInt(const Json::Value& v, const std::string& key, int64_t def = 0) {
    if (v.isMember(key) && v[key].isInt64()) return v[key].asInt64();
    if (v.isMember(key) && v[key].isString()) {
        try { return std::stoll(v[key].asString()); } catch (...) {}
    }
    return def;
}

std::string ToJson(const Json::Value& v) {
    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    return Json::writeString(wb, v);
}

Json::Value ParseArgs(const std::string& args) {
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::istringstream stream(args);
    std::string errors;
    Json::parseFromStream(builder, stream, &root, &errors);
    return root;
}

} // namespace

AgendaTool::AgendaTool(AgendaStore* store)
    : m_store(store) {}

ToolDefinition AgendaTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "agenda";
    def.description =
        "Manage your personal calendar/agenda. Create, list, update, delete, "
        "and complete events. Events are private to you (scoped to your agent). "
        "Supports one-shot and recurring events (daily/weekly/monthly).";
    def.resultMode = ToolResultMode::deliver_to_model;

    def.parameters.push_back({"action", "string",
        "The agenda operation: create, list, update, delete, complete, view",
        true, "", {"create", "list", "update", "delete", "complete", "view"}});

    // create fields
    def.parameters.push_back({"title", "string",
        "Event title (required for create, update)", false});
    def.parameters.push_back({"start_time", "string",
        "Event start time in ISO-8601 format, e.g. '2026-08-17T09:00:00+02:00' (required for create, update)",
        false});
    def.parameters.push_back({"timezone", "string",
        "IANA timezone for the event, e.g. 'Europe/Amsterdam' (default: UTC)", false});
    def.parameters.push_back({"description", "string",
        "Optional event description", false});
    def.parameters.push_back({"end_time", "string",
        "Optional event end time in ISO-8601 format", false});
    def.parameters.push_back({"recurrence", "string",
        "Recurrence pattern: none, daily, weekly, monthly (default: none)",
        false, "", {"none", "daily", "weekly", "monthly"}});

    // update/delete/complete/view fields
    def.parameters.push_back({"event_id", "integer",
        "Event ID (required for update, delete, complete, view)", false});

    // list fields
    def.parameters.push_back({"include_completed", "boolean",
        "Include completed events in list (default: false)", false});

    return def;
}

ToolResult AgendaTool::Execute(const ToolCall& call) {
    ToolResult result;
    auto args = ParseArgs(call.arguments);

    std::string agentId = GetStr(args, "__agent_id", "");
    std::string action = GetStr(args, "action", "");

    if (agentId.empty()) {
        result.success = false;
        result.output = "Error: agent identity not available";
        return result;
    }

    if (!m_store) {
        result.success = false;
        result.output = "Error: agenda store not available";
        return result;
    }

    if (action == "create") return DoCreate(args, agentId);
    if (action == "list") return DoList(args, agentId);
    if (action == "update") return DoUpdate(args, agentId);
    if (action == "delete") return DoDelete(args, agentId);
    if (action == "complete") return DoComplete(args, agentId);
    if (action == "view") return DoView(args, agentId);

    result.success = false;
    result.output = "Error: unknown action '" + action + "'. Valid: create, list, update, delete, complete, view";
    return result;
}

ToolResult AgendaTool::DoCreate(const Json::Value& args, const std::string& agentId) {
    ToolResult result;
    std::string title = GetStr(args, "title");
    std::string startTime = GetStr(args, "start_time");
    std::string timezone = GetStr(args, "timezone", "UTC");
    std::string description = GetStr(args, "description");
    std::string endTime = GetStr(args, "end_time");
    std::string recurrence = GetStr(args, "recurrence", "none");

    if (title.empty()) {
        result.success = false;
        result.output = "Error: title is required for create";
        return result;
    }
    if (startTime.empty()) {
        result.success = false;
        result.output = "Error: start_time is required for create (ISO-8601 format)";
        return result;
    }

    auto ev = m_store->Create(agentId, title, startTime, timezone,
                                description, endTime, recurrence);
    result.success = true;
    result.output = "Created event #" + std::to_string(ev.id) + ": " + title +
                    " at " + startTime + " (" + timezone + ")";
    return result;
}

ToolResult AgendaTool::DoList(const Json::Value& args, const std::string& agentId) {
    ToolResult result;
    auto events = m_store->ListForAgent(agentId);

    Json::Value root(Json::arrayValue);
    for (const auto& ev : events) {
        if (ev.completed) continue; // skip completed by default
        Json::Value j;
        j["id"] = (Json::Int64)ev.id;
        j["title"] = ev.title;
        j["start_time"] = ev.start_time;
        j["timezone"] = ev.timezone;
        j["recurrence"] = ev.recurrence;
        j["completed"] = ev.completed;
        if (!ev.description.empty()) j["description"] = ev.description;
        if (!ev.end_time.empty()) j["end_time"] = ev.end_time;
        root.append(j);
    }

    result.success = true;
    result.output = ToJson(root);
    return result;
}

ToolResult AgendaTool::DoUpdate(const Json::Value& args, const std::string& agentId) {
    ToolResult result;
    int64_t id = GetInt(args, "event_id");
    std::string title = GetStr(args, "title");
    std::string startTime = GetStr(args, "start_time");
    std::string timezone = GetStr(args, "timezone", "UTC");
    std::string description = GetStr(args, "description");
    std::string endTime = GetStr(args, "end_time");
    std::string recurrence = GetStr(args, "recurrence", "none");

    if (id == 0) {
        result.success = false;
        result.output = "Error: event_id is required for update";
        return result;
    }
    if (title.empty() || startTime.empty()) {
        result.success = false;
        result.output = "Error: title and start_time are required for update";
        return result;
    }

    auto ev = m_store->Update(id, agentId, title, startTime, timezone,
                               description, endTime, recurrence);
    if (!ev) {
        result.success = false;
        result.output = "Error: event #" + std::to_string(id) + " not found";
        return result;
    }

    result.success = true;
    result.output = "Updated event #" + std::to_string(id) + ": " + title;
    return result;
}

ToolResult AgendaTool::DoDelete(const Json::Value& args, const std::string& agentId) {
    ToolResult result;
    int64_t id = GetInt(args, "event_id");

    if (id == 0) {
        result.success = false;
        result.output = "Error: event_id is required for delete";
        return result;
    }

    bool ok = m_store->Delete(id, agentId);
    result.success = ok;
    result.output = ok ? "Deleted event #" + std::to_string(id)
                       : "Error: event #" + std::to_string(id) + " not found";
    return result;
}

ToolResult AgendaTool::DoComplete(const Json::Value& args, const std::string& agentId) {
    ToolResult result;
    int64_t id = GetInt(args, "event_id");
    bool completed = true;
    if (args.isMember("completed") && args["completed"].isBool()) {
        completed = args["completed"].asBool();
    }

    if (id == 0) {
        result.success = false;
        result.output = "Error: event_id is required for complete";
        return result;
    }

    bool ok = m_store->SetCompleted(id, agentId, completed);
    result.success = ok;
    result.output = ok ? (completed ? "Marked event #" + std::to_string(id) + " as completed"
                                     : "Marked event #" + std::to_string(id) + " as not completed")
                       : "Error: event #" + std::to_string(id) + " not found";
    return result;
}

ToolResult AgendaTool::DoView(const Json::Value& args, const std::string& agentId) {
    ToolResult result;
    int64_t id = GetInt(args, "event_id");

    if (id == 0) {
        result.success = false;
        result.output = "Error: event_id is required for view";
        return result;
    }

    auto ev = m_store->GetById(id, agentId);
    if (!ev) {
        result.success = false;
        result.output = "Error: event #" + std::to_string(id) + " not found";
        return result;
    }

    result.success = true;
    result.output = EventToJson(*ev);
    return result;
}

std::string AgendaTool::EventToJson(const AgendaEvent& ev) const {
    Json::Value j;
    j["id"] = (Json::Int64)ev.id;
    j["title"] = ev.title;
    j["start_time"] = ev.start_time;
    j["timezone"] = ev.timezone;
    j["recurrence"] = ev.recurrence;
    j["completed"] = ev.completed;
    if (!ev.description.empty()) j["description"] = ev.description;
    if (!ev.end_time.empty()) j["end_time"] = ev.end_time;
    return ToJson(j);
}

} // namespace animus::kernel
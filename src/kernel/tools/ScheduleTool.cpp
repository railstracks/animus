#include "animus_kernel/tools/ScheduleTool.h"

#include <json/json.h>
#include <json/writer.h>

#include <iostream>

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

std::string GetString(const Json::Value& obj, const std::string& key,
                      const std::string& defaultVal = "") {
    if (obj.isMember(key) && obj[key].isString()) return obj[key].asString();
    return defaultVal;
}

bool GetBool(const Json::Value& obj, const std::string& key, bool defaultVal = false) {
    if (obj.isMember(key) && obj[key].isBool()) return obj[key].asBool();
    return defaultVal;
}

int GetInt(const Json::Value& obj, const std::string& key, int defaultVal = 0) {
    if (obj.isMember(key) && obj[key].isInt()) return obj[key].asInt();
    return defaultVal;
}

std::string SerializeSchedules(const std::vector<ScheduleDescriptor>& schedules) {
    Json::Value arr(Json::arrayValue);
    for (const auto& s : schedules) {
        Json::Value item(Json::objectValue);
        item["id"] = s.id;
        item["tag"] = s.tag;
        item["type"] = s.type == ScheduleType::Recurring ? "recurring" : "one_shot";
        item["next_fire"] = s.next_fire;
        item["cron_expr"] = s.cron_expr.empty() ? Json::Value(Json::nullValue) : Json::Value(s.cron_expr);
        item["timezone"] = s.timezone;
        // Truncate message for list display
        const auto preview = s.message.size() > 80
            ? s.message.substr(0, 80) + "..."
            : s.message;
        item["message_preview"] = preview;
        item["enabled"] = s.enabled;
        item["fire_count"] = s.fire_count;
        item["last_fire"] = s.last_fire.empty() ? Json::Value(Json::nullValue) : Json::Value(s.last_fire);
        arr.append(item);
    }
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, arr);
}

} // anonymous namespace

// ============================================================================
// Construction
// ============================================================================

ScheduleTool::ScheduleTool(Scheduler* scheduler) : m_scheduler(scheduler) {}

// ============================================================================
// Definition
// ============================================================================

ToolDefinition ScheduleTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "schedule";
    def.description =
        "Schedule a future action. Use for reminders, periodic checks, routines, "
        "and autonomous workflows. When the schedule fires, the system will create "
        "a session with your context message. Schedules survive restarts.";
    def.resultMode = ToolResultMode::deliver_to_model;

    ToolParameter actionParam;
    actionParam.name = "action";
    actionParam.type = "string";
    actionParam.description = "Operation: 'set' to create, 'list' to view, 'cancel' to remove.";
    actionParam.required = true;
    actionParam.enum_values = {"set", "list", "cancel"};
    def.parameters.push_back(actionParam);

    // set parameters
    ToolParameter whenParam;
    whenParam.name = "when";
    whenParam.type = "string";
    whenParam.description =
        "When to trigger. ISO timestamp (2026-05-10T09:00:00Z), relative time "
        "(in 30 minutes, tomorrow at 9am), or cron expression (0 9 * * 1 for "
        "every Monday at 9am UTC). Cron expressions create recurring schedules.";
    whenParam.required = false;
    def.parameters.push_back(whenParam);

    ToolParameter msgParam;
    msgParam.name = "message";
    msgParam.type = "string";
    msgParam.description = "Context message delivered when the schedule fires.";
    msgParam.required = false;
    def.parameters.push_back(msgParam);

    ToolParameter tagParam;
    tagParam.name = "tag";
    tagParam.type = "string";
    tagParam.description = "Optional grouping tag (e.g. 'reminder', 'healthcheck').";
    tagParam.required = false;
    def.parameters.push_back(tagParam);

    ToolParameter repeatParam;
    repeatParam.name = "repeat";
    repeatParam.type = "boolean";
    repeatParam.description = "Whether schedule is recurring. Only meaningful with cron expressions.";
    repeatParam.required = false;
    def.parameters.push_back(repeatParam);

    // list / cancel parameters
    ToolParameter idParam;
    idParam.name = "id";
    idParam.type = "string";
    idParam.description = "Schedule ID (for cancel).";
    idParam.required = false;
    def.parameters.push_back(idParam);

    return def;
}

// ============================================================================
// Execute
// ============================================================================

ToolResult ScheduleTool::Execute(const ToolCall& call) {
    ToolResult result;
    result.call_id = call.id;

    auto args = ParseArgs(call.arguments);
    if (args.isNull()) {
        result.success = false;
        result.error = "Failed to parse tool arguments as JSON";
        return result;
    }

    const std::string action = GetString(args, "action");
    if (action.empty()) {
        result.success = false;
        result.error = "Missing required parameter: action";
        return result;
    }

    // agent_id injected by ChainRunner
    const std::string agentId = GetString(args, "__agent_id", "");

    if (action == "set") {
        return HandleSet(call.id, agentId, call.arguments);
    } else if (action == "list") {
        return HandleList(call.id, agentId, call.arguments);
    } else if (action == "cancel") {
        return HandleCancel(call.id, agentId, call.arguments);
    }

    result.success = false;
    result.error = "Unknown action: " + action;
    return result;
}

// ============================================================================
// HandleSet
// ============================================================================

ToolResult ScheduleTool::HandleSet(const std::string& callId,
                                     const std::string& agentId,
                                     const std::string& argsJson) {
    ToolResult result;
    result.call_id = callId;

    auto args = ParseArgs(argsJson);
    const std::string when = GetString(args, "when");
    if (when.empty()) {
        result.success = false;
        result.error = "Missing required parameter: when";
        return result;
    }

    ScheduleDescriptor sched;
    sched.agent_id = agentId;
    sched.tag = GetString(args, "tag", "");
    if (sched.tag.empty()) {
        // Use a default tag so the session key has a meaningful prefix.
        // Without this, the scheduled session key becomes "scheduled::"
        // (empty tag + empty agent fallback), creating a ghost session.
        sched.tag = "scheduled";
    }
    sched.next_fire = when;
    sched.timezone = "UTC"; // can be refined later
    sched.message = GetString(args, "message", "");
    sched.type = GetBool(args, "repeat", false) ? ScheduleType::Recurring : ScheduleType::OneShot;

    // Check if updating existing
    const std::string existingId = GetString(args, "id", "");
    if (!existingId.empty()) {
        auto existing = m_scheduler->Get(existingId);
        if (!existing) {
            result.success = false;
            result.error = "Schedule not found: " + existingId;
            return result;
        }
        sched.id = existingId;
    }

    std::string error;
    const std::string newId = m_scheduler->Create(sched, &error);
    if (newId.empty()) {
        result.success = false;
        result.error = "Failed to create schedule: " + error;
        return result;
    }

    Json::Value out(Json::objectValue);
    out["id"] = newId;
    out["next_fire"] = when;
    out["message"] = "Schedule created: " + newId;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    result.output = Json::writeString(wb, out);
    result.success = true;
    return result;
}

// ============================================================================
// HandleList
// ============================================================================

ToolResult ScheduleTool::HandleList(const std::string& callId,
                                      const std::string& agentId,
                                      const std::string& argsJson) {
    ToolResult result;
    result.call_id = callId;

    auto args = ParseArgs(argsJson);
    const std::string tag = GetString(args, "tag", "");

    const auto schedules = m_scheduler->List(agentId, tag);

    Json::Value out(Json::objectValue);
    out["count"] = static_cast<int>(schedules.size());

    Json::Value arr(Json::arrayValue);
    for (const auto& s : schedules) {
        Json::Value item(Json::objectValue);
        item["id"] = s.id;
        item["tag"] = s.tag;
        item["type"] = s.type == ScheduleType::Recurring ? "recurring" : "one_shot";
        item["next_fire"] = s.next_fire;
        item["cron_expr"] = s.cron_expr.empty() ? Json::Value(Json::nullValue) : Json::Value(s.cron_expr);
        item["message"] = s.message;
        item["enabled"] = s.enabled;
        item["fire_count"] = s.fire_count;
        item["last_fire"] = s.last_fire.empty() ? Json::Value(Json::nullValue) : Json::Value(s.last_fire);
        arr.append(item);
    }
    out["schedules"] = arr;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    result.output = Json::writeString(wb, out);
    result.success = true;
    return result;
}

// ============================================================================
// HandleCancel
// ============================================================================

ToolResult ScheduleTool::HandleCancel(const std::string& callId,
                                        const std::string& /*agentId*/,
                                        const std::string& argsJson) {
    ToolResult result;
    result.call_id = callId;

    auto args = ParseArgs(argsJson);
    const std::string id = GetString(args, "id");
    if (id.empty()) {
        result.success = false;
        result.error = "Missing required parameter: id";
        return result;
    }

    std::string error;
    if (!m_scheduler->Cancel(id, &error)) {
        result.success = false;
        result.error = "Failed to cancel schedule: " + error;
        return result;
    }

    Json::Value out(Json::objectValue);
    out["cancelled"] = id;
    out["message"] = "Schedule cancelled: " + id;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    result.output = Json::writeString(wb, out);
    result.success = true;
    return result;
}

} // namespace animus::kernel

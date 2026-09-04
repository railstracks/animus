#include "animus_kernel/tools/ApiTool.h"

#include "animus_kernel/ApiPackageStore.h"
#include "animus_kernel/Log.h"

#include <json/json.h>

#include <filesystem>
#include <set>
#include <sstream>

namespace animus::kernel {

namespace fs = std::filesystem;

namespace {

std::string JsonWrite(const Json::Value& v) {
    Json::StreamWriterBuilder b;
    b["indentation"] = "";
    b["commentStyle"] = "None";
    return Json::writeString(b, v);
}

std::vector<std::string> Tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream iss(line);
    std::string tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}

}  // namespace

ToolDefinition ApiTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "api";
    def.description =
        "Invoke installed API packages. Management: 'api status' (inventory), "
        "'api enable|disable <package>', 'api package create|read|delete <name>', "
        "'api command read <pkg> <cmd>', 'api files <pkg> list'. "
        "Introspection: 'api <package>' lists its commands. "
        "Invocation: 'api <package> <command> {json args}' runs it — e.g. "
        "'api alpaca portfolio list' or 'api alpaca create_order {\"symbol\": \"NVDA\"}'.";
    ToolParameter input;
    input.name = "input";
    input.type = "string";
    input.description = "api command line (verb + arguments, or package invocation)";
    input.required = true;
    def.parameters.push_back(input);
    def.resultMode = ToolResultMode::deliver_to_model;
    return def;
}

std::string ApiTool::AvailablePackagesLine() const {
    std::string names;
    for (const auto& p : m_runtime->store()->ListPackages())
        names += (names.empty() ? "" : ", ") + p.name + (p.enabled ? "" : " (disabled)");
    return "installed packages: " + (names.empty() ? "none" : names);
}

ToolResult ApiTool::Execute(const ToolCall& call) {
    ToolResult result;
    result.call_id = call.id;

    Json::Value args;
    {
        Json::CharReaderBuilder b;
        std::istringstream iss(call.arguments);
        std::string err;
        if (!Json::parseFromStream(b, iss, &args, &err) || !args.isObject() ||
            !args.isMember("input")) {
            result.success = false;
            result.error = "api tool expects {\"input\": \"...\"}";
            return result;
        }
    }
    const std::string input = args["input"].asString();
    const auto tokens = Tokenize(input);
    if (tokens.empty()) {
        result.success = false;
        result.error = "empty input. " + AvailablePackagesLine() +
                       " — 'api status' for full inventory.";
        return result;
    }

    static const std::set<std::string> kReserved = {
        "package", "command", "connection", "files", "download", "upload",
        "enable", "disable", "status"};

    try {
        std::string out;
        const std::string& first = tokens[0];
        if (first == "status") out = HandleStatus(tokens);
        else if (first == "package") out = HandlePackage(tokens);
        else if (first == "command") out = HandleCommand(tokens);
        else if (first == "files") out = HandleFiles(tokens);
        else if (first == "enable") out = HandleEnable(tokens.size() > 1 ? tokens[1] : "", true);
        else if (first == "disable") out = HandleEnable(tokens.size() > 1 ? tokens[1] : "", false);
        else if (first == "download" || first == "upload")
            out = "registry " + first + " arrives with the registry pipeline (#61) — packages are "
                   "created locally via 'api package create' for now";
        else if (first == "connection")
            out = "connection management applies at runtime in build order (d); configured "
                   "connections appear in 'api status <package>' once enabled";
        else if (kReserved.count(first))
            out = "reserved verb '" + first + "' is not implemented yet";
        else out = HandleInvocation(tokens, input);

        result.success = true;
        result.output = out;
    } catch (const std::exception& e) {
        result.success = false;
        result.error = e.what();
    }
    return result;
}

// ----------------------------------------------------------------------------

std::string ApiTool::HandleStatus(const std::vector<std::string>& tokens) {
    auto packages = m_runtime->store()->ListPackages();
    if (tokens.size() > 1) {
        const std::string& name = tokens[1];
        auto pkg = m_runtime->store()->GetPackageByName(name);
        if (!pkg) return "unknown package '" + name + "'. " + AvailablePackagesLine();
        Json::Value out(Json::objectValue);
        out["name"] = pkg->name;
        out["enabled"] = pkg->enabled;
        out["version"] = pkg->version;
        out["locally_modified"] = pkg->locally_modified;
        Json::Value cmds(Json::arrayValue);
        for (const auto& c : m_runtime->store()->ListCommands(pkg->id))
            cmds.append(c.kind == "action" ? c.name : c.name + " (hook:" + c.event + ")");
        out["commands"] = cmds;
        Json::Value conns(Json::arrayValue);
        for (const auto& c : m_runtime->store()->ListConnections(pkg->id))
            conns.append(c.name + ":" + c.type + (c.enabled ? "" : " (disabled)"));
        out["connections"] = conns;
        return JsonWrite(out);
    }
    if (packages.empty()) return "no packages installed. create one: 'api package create <name> <description>'";
    Json::Value out(Json::objectValue);
    Json::Value list(Json::arrayValue);
    for (const auto& p : packages) {
        Json::Value e(Json::objectValue);
        e["name"] = p.name;
        e["enabled"] = p.enabled;
        e["version"] = p.version;
        size_t actions = 0, hooks = 0;
        for (const auto& c : m_runtime->store()->ListCommands(p.id))
            (c.kind == "action" ? actions : hooks)++;
        e["actions"] = static_cast<Json::UInt64>(actions);
        e["hooks"] = static_cast<Json::UInt64>(hooks);
        list.append(e);
    }
    out["packages"] = list;
    return JsonWrite(out);
}

std::string ApiTool::HandlePackage(const std::vector<std::string>& tokens) {
    if (tokens.size() < 3)
        return "usage: api package create <name> <description...> | read <name> | delete <name>";
    const std::string& verb = tokens[1];
    const std::string& name = tokens[2];
    auto* store = m_runtime->store();

    if (verb == "read") {
        auto pkg = store->GetPackageByName(name);
        if (!pkg) return "unknown package '" + name + "'. " + AvailablePackagesLine();
        Json::Value out(Json::objectValue);
        out["name"] = pkg->name;
        out["display_name"] = pkg->display_name;
        out["description"] = pkg->description;
        out["version"] = pkg->version;
        out["enabled"] = pkg->enabled;
        out["state_schema"] = pkg->state_schema;  // schema is authored config (no values)
        return JsonWrite(out);
    }
    if (verb == "create") {
        if (tokens.size() < 4) return "usage: api package create <name> <description...>";
        std::string desc;
        for (size_t i = 3; i < tokens.size(); ++i) desc += (desc.empty() ? "" : " ") + tokens[i];
        ApiPackage pkg;
        pkg.name = name;
        pkg.description = desc;
        pkg.version = "0.1.0";
        store->CreatePackage(pkg);  // throws on duplicate/reserved handled by store
        ALOG_INFO("api", "[api] package created: " << name);
        return "created '" + name + "' (disabled). add commands with the registry or "
               "InstallFromManifest; enable with 'api enable " + name + "'.";
    }
    if (verb == "delete") {
        auto pkg = store->GetPackageByName(name);
        if (!pkg) return "unknown package '" + name + "'. " + AvailablePackagesLine();
        store->DeletePackage(pkg->id);
        std::error_code ec;
        fs::remove_all(fs::path(m_runtime->config().filesRoot) / name, ec);
        ALOG_INFO("api", "[api] package deleted: " << name);
        return "deleted '" + name + "' (commands, connections, state, filespace).";
    }
    return "unsupported package verb '" + verb + "' (create, read, delete)";
}

std::string ApiTool::HandleCommand(const std::vector<std::string>& tokens) {
    if (tokens.size() < 4) return "usage: api command read <package> <command>";
    if (tokens[1] != "read")
        return "command authoring arrives with the registry pipeline — use manifests or "
               "'api command read' to inspect";
    auto pkg = m_runtime->store()->GetPackageByName(tokens[2]);
    if (!pkg) return "unknown package '" + tokens[2] + "'. " + AvailablePackagesLine();
    auto cmd = m_runtime->store()->GetCommand(pkg->id, tokens[3]);
    if (!cmd) {
        std::string avail;
        for (const auto& c : m_runtime->store()->ListCommands(pkg->id))
            avail += (avail.empty() ? "" : ", ") + c.name;
        return "unknown command '" + tokens[3] + "' (available: " + (avail.empty() ? "none" : avail) + ")";
    }
    Json::Value out(Json::objectValue);
    out["package"] = pkg->name;
    out["name"] = cmd->name;
    out["kind"] = cmd->kind;
    if (!cmd->event.empty()) out["event"] = cmd->event;
    out["description"] = cmd->description;
    out["parameters"] = cmd->parameters.empty() ? "{}" : cmd->parameters;
    out["request"] = cmd->request.empty() ? Json::Value() : cmd->request;
    return JsonWrite(out);
}

std::string ApiTool::HandleFiles(const std::vector<std::string>& tokens) {
    if (tokens.size() < 3 || tokens[1] != "list")
        return "usage: api files <package> list";
    auto pkg = m_runtime->store()->GetPackageByName(tokens[2]);
    if (!pkg) return "unknown package '" + tokens[2] + "'. " + AvailablePackagesLine();
    Json::Value out(Json::objectValue);
    out["package"] = pkg->name;
    out["quota_mb"] = static_cast<Json::Int64>(pkg->files_quota_mb);
    Json::Value files(Json::arrayValue);
    int64_t used = 0;
    std::error_code ec;
    fs::path root = fs::path(m_runtime->config().filesRoot) / pkg->name;
    for (fs::directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        Json::Value f(Json::objectValue);
        f["name"] = it->path().filename().string();
        f["size"] = static_cast<Json::Int64>(it->file_size(ec));
        used += f["size"].asInt64();
        files.append(f);
    }
    out["files"] = files;
    out["used_bytes"] = used;
    return JsonWrite(out);
}

std::string ApiTool::HandleEnable(const std::string& name, bool enable) {
    if (name.empty()) return "usage: api enable|disable <package>";
    auto* store = m_runtime->store();
    auto pkg = store->GetPackageByName(name);
    if (!pkg) return "unknown package '" + name + "'. " + AvailablePackagesLine();
    store->SetPackageEnabled(pkg->id, enable);
    ALOG_INFO("api", "[api] package " << (enable ? "enabled" : "disabled") << ": " << name);
    // Runtime apply for passive connections arrives in build order (d);
    // enabling now flips availability of action commands immediately.
    return std::string("package '") + name + (enable ? "' enabled" : "' disabled") +
           (enable ? " — commands are now invocable. Passive connections start when the "
                     "connection runtime lands (d)." : ".");
}

// Invocation: `api <pkg> <rest>` where rest starts with the (possibly
// space-containing) command name followed by an optional JSON object.
std::string ApiTool::HandleInvocation(const std::vector<std::string>& tokens,
                                      const std::string& input) {
    const std::string& pkgName = tokens[0];
    auto pkg = m_runtime->store()->GetPackageByName(pkgName);
    if (!pkg) return "unknown package '" + pkgName + "'. " + AvailablePackagesLine();

    // Longest command-name match against the remaining input.
    std::string rest = input.substr(input.find(pkgName) + pkgName.size());
    while (!rest.empty() && rest.front() == ' ') rest.erase(0, 1);

    std::string best;
    for (const auto& c : m_runtime->store()->ListCommands(pkg->id)) {
        if (c.kind != "action") continue;
        if (rest.rfind(c.name, 0) == 0 && c.name.size() > best.size()) best = c.name;
    }
    if (best.empty()) {
        std::string avail;
        for (const auto& s : m_runtime->ListActions(pkgName, m_currentAgentId))
            avail += (avail.empty() ? "" : ", ") + s.name;
        return "no command matches '" + rest + "' in '" + pkgName + "' (actions: " +
               (avail.empty() ? "none" : avail) + ")";
    }

    std::string argsText = rest.substr(best.size());
    while (!argsText.empty() && (argsText.front() == ' ')) argsText.erase(0, 1);

    Json::Value args(Json::objectValue);
    if (!argsText.empty()) {
        std::string err;
        Json::CharReaderBuilder b;
        std::istringstream iss(argsText);
        if (!Json::parseFromStream(b, iss, &args, &err)) {
            return "arguments must be a JSON object after the command name (got: '" +
                   argsText.substr(0, 80) + "')";
        }
    }

    Json::Value out = m_runtime->ExecuteAction(pkgName, best, m_currentAgentId, args);
    return JsonWrite(out);
}

}  // namespace animus::kernel

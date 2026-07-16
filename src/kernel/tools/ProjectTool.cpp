#include "animus_kernel/tools/ProjectTool.h"
#include "animus_kernel/SchemaHelpers.h"
#include "animus_kernel/PgDataStore.h"

#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <set>
#include <map>
#include <atomic>

namespace animus::kernel {

// ============================================================================
// Helpers
// ============================================================================

namespace {

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string GenerateId() {
    static thread_local std::mt19937_64 rng(
        static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    static std::atomic<std::uint64_t> counter{0};
    std::uniform_int_distribution<std::uint64_t> dist;

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(16) << dist(rng)
        << std::setw(16) << counter.fetch_add(1);
    return oss.str();
}

} // namespace

// ============================================================================
// ProjectStore
// ============================================================================

ProjectStore::ProjectStore(IDataStore* store)
    : m_store(store) {
    EnsureSchema();
}

void ProjectStore::EnsureSchema() {
    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS projects (
            id TEXT PRIMARY KEY,
            agent_id TEXT NOT NULL,
            title TEXT NOT NULL,
            description TEXT NOT NULL DEFAULT '',
            status TEXT NOT NULL DEFAULT 'active',
            created_at BIGINT NOT NULL,
            updated_at BIGINT NOT NULL
        )
    )");

    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS project_tasks (
            id TEXT PRIMARY KEY,
            project_id TEXT NOT NULL,
            agent_id TEXT NOT NULL DEFAULT '',
            title TEXT NOT NULL,
            detail TEXT NOT NULL DEFAULT '',
            status TEXT NOT NULL DEFAULT 'pending',
            depends_on TEXT NOT NULL DEFAULT '[]',
            position INTEGER NOT NULL DEFAULT 0,
            result TEXT NOT NULL DEFAULT '',
            created_at BIGINT NOT NULL,
            updated_at BIGINT NOT NULL
        )
    )");

    // Backfill agent_id column if it doesn't exist (for existing installs)
    if (!schema::ColumnExists(m_store, "project_tasks", "agent_id")) {
        try { m_store->Exec("ALTER TABLE project_tasks ADD COLUMN agent_id TEXT NOT NULL DEFAULT ''"); }
        catch (...) {}
    }
}

// ============================================================================
// Projects
// ============================================================================

Project ProjectStore::CreateProject(const std::string& agentId, const std::string& title, const std::string& description) {
    Project p;
    p.id = GenerateId();
    p.agent_id = agentId;
    p.title = title;
    p.description = description;
    p.status = "active";
    p.created_at = NowMs();
    p.updated_at = p.created_at;

    auto stmt = m_store->Prepare(
        "INSERT INTO projects (id, agent_id, title, description, status, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)");
    stmt->BindText(1, p.id);
    stmt->BindText(2, p.agent_id);
    stmt->BindText(3, p.title);
    stmt->BindText(4, p.description);
    stmt->BindText(5, p.status);
    stmt->BindInt64(6, p.created_at);
    stmt->BindInt64(7, p.updated_at);
    stmt->Step();

    return p;
}

std::vector<Project> ProjectStore::ListProjects(const std::string& agentId, const std::string& statusFilter) {
    std::string sql = "SELECT id, title, description, status, created_at, updated_at FROM projects WHERE agent_id = ?";
    if (!statusFilter.empty()) sql += " AND status = ?";
    sql += " ORDER BY created_at DESC";

    auto stmt = m_store->Prepare(sql);
    stmt->BindText(1, agentId);
    if (!statusFilter.empty()) stmt->BindText(2, statusFilter);

    std::vector<Project> result;
    while (stmt->Step()) {
        Project p;
        p.id = stmt->ColumnText(0);
        p.agent_id = agentId;
        p.title = stmt->ColumnText(1);
        p.description = stmt->ColumnText(2);
        p.status = stmt->ColumnText(3);
        p.created_at = stmt->ColumnInt64(4);
        p.updated_at = stmt->ColumnInt64(5);
        result.push_back(p);
    }
    return result;
}

std::optional<Project> ProjectStore::GetProject(const std::string& agentId, const std::string& projectId) {
    auto stmt = m_store->Prepare(
        "SELECT id, title, description, status, created_at, updated_at FROM projects "
        "WHERE id = ? AND agent_id = ?");
    stmt->BindText(1, projectId);
    stmt->BindText(2, agentId);

    if (stmt->Step()) {
        Project p;
        p.id = stmt->ColumnText(0);
        p.agent_id = agentId;
        p.title = stmt->ColumnText(1);
        p.description = stmt->ColumnText(2);
        p.status = stmt->ColumnText(3);
        p.created_at = stmt->ColumnInt64(4);
        p.updated_at = stmt->ColumnInt64(5);
        return p;
    }
    return std::nullopt;
}

bool ProjectStore::UpdateProjectStatus(const std::string& agentId, const std::string& projectId, const std::string& status) {
    auto stmt = m_store->Prepare(
        "UPDATE projects SET status = ?, updated_at = ? WHERE id = ? AND agent_id = ?");
    stmt->BindText(1, status);
    stmt->BindInt64(2, NowMs());
    stmt->BindText(3, projectId);
    stmt->BindText(4, agentId);
    stmt->Step();
    return schema::DidAffectRows(m_store);
}

// ============================================================================
// Tasks
// ============================================================================

namespace {

std::string JoinStrings(const std::vector<std::string>& vec, const std::string& delim = ",") {
    Json::Value arr(Json::arrayValue);
    for (const auto& s : vec) arr.append(s);
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, arr);
}

std::vector<std::string> ParseStringArray(const std::string& json) {
    std::vector<std::string> result;
    if (json.empty()) return result;
    Json::Value parsed;
    Json::CharReaderBuilder builder;
    auto* reader = builder.newCharReader();
    reader->parse(json.c_str(), json.c_str() + json.size(), &parsed, nullptr);
    delete reader;
    if (parsed.isArray()) {
        for (const auto& v : parsed) result.push_back(v.asString());
    }
    return result;
}

} // namespace

ProjectTask ProjectStore::AddTask(const std::string& agentId, const std::string& projectId,
                                   const std::string& title, const std::string& detail,
                                   const std::vector<std::string>& dependsOn) {
    ProjectTask t;
    t.id = GenerateId();
    t.project_id = projectId;
    t.title = title;
    t.detail = detail;
    t.status = dependsOn.empty() ? "ready" : "pending";
    t.depends_on = dependsOn;

    // Get next position
    auto posStmt = m_store->Prepare(
        "SELECT COALESCE(MAX(position), -1) + 1 FROM project_tasks WHERE project_id = ?");
    posStmt->BindText(1, projectId);
    if (posStmt->Step()) t.position = static_cast<int>(posStmt->ColumnInt64(0));

    t.created_at = NowMs();
    t.updated_at = t.created_at;

    auto stmt = m_store->Prepare(
        "INSERT INTO project_tasks (id, project_id, agent_id, title, detail, status, depends_on, position, result, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    stmt->BindText(1, t.id);
    stmt->BindText(2, t.project_id);
    stmt->BindText(3, agentId);
    stmt->BindText(4, t.title);
    stmt->BindText(5, t.detail);
    stmt->BindText(6, t.status);
    stmt->BindText(7, JoinStrings(t.depends_on));
    stmt->BindInt64(8, t.position);
    stmt->BindText(9, t.result);
    stmt->BindInt64(10, t.created_at);
    stmt->BindInt64(11, t.updated_at);
    stmt->Step();

    return t;
}

std::vector<ProjectTask> ProjectStore::ListTasks(const std::string& agentId, const std::string& projectId,
                                                  const std::string& statusFilter) {
    std::string sql =
        "SELECT id, project_id, title, detail, status, depends_on, position, result, created_at, updated_at "
        "FROM project_tasks WHERE agent_id = ? AND project_id = ?";
    if (!statusFilter.empty()) sql += " AND status = ?";
    sql += " ORDER BY position ASC";

    auto stmt = m_store->Prepare(sql);
    stmt->BindText(1, agentId);
    stmt->BindText(2, projectId);
    if (!statusFilter.empty()) stmt->BindText(3, statusFilter);

    std::vector<ProjectTask> result;
    while (stmt->Step()) {
        ProjectTask t;
        t.id = stmt->ColumnText(0);
        t.project_id = stmt->ColumnText(1);
        t.title = stmt->ColumnText(2);
        t.detail = stmt->ColumnText(3);
        t.status = stmt->ColumnText(4);
        t.depends_on = ParseStringArray(stmt->ColumnText(5));
        t.position = static_cast<int>(stmt->ColumnInt64(6));
        t.result = stmt->ColumnText(7);
        t.created_at = stmt->ColumnInt64(8);
        t.updated_at = stmt->ColumnInt64(9);
        result.push_back(t);
    }
    return result;
}

std::optional<ProjectTask> ProjectStore::GetTask(const std::string& agentId, const std::string& taskId) {
    auto stmt = m_store->Prepare(
        "SELECT id, project_id, title, detail, status, depends_on, position, result, created_at, updated_at "
        "FROM project_tasks WHERE id = ? AND agent_id = ?");
    stmt->BindText(1, taskId);
    stmt->BindText(2, agentId);

    if (stmt->Step()) {
        ProjectTask t;
        t.id = stmt->ColumnText(0);
        t.project_id = stmt->ColumnText(1);
        t.title = stmt->ColumnText(2);
        t.detail = stmt->ColumnText(3);
        t.status = stmt->ColumnText(4);
        t.depends_on = ParseStringArray(stmt->ColumnText(5));
        t.position = static_cast<int>(stmt->ColumnInt64(6));
        t.result = stmt->ColumnText(7);
        t.created_at = stmt->ColumnInt64(8);
        t.updated_at = stmt->ColumnInt64(9);
        return t;
    }
    return std::nullopt;
}

bool ProjectStore::UpdateTask(const std::string& agentId, const ProjectTask& task) {
    auto stmt = m_store->Prepare(
        "UPDATE project_tasks SET title = ?, detail = ?, status = ?, depends_on = ?, result = ?, updated_at = ? "
        "WHERE id = ? AND agent_id = ?");
    stmt->BindText(1, task.title);
    stmt->BindText(2, task.detail);
    stmt->BindText(3, task.status);
    stmt->BindText(4, JoinStrings(task.depends_on));
    stmt->BindText(5, task.result);
    stmt->BindInt64(6, NowMs());
    stmt->BindText(7, task.id);
    stmt->BindText(8, agentId);
    stmt->Step();
    return schema::DidAffectRows(m_store);
}

std::optional<ProjectTask> ProjectStore::NextReady(const std::string& agentId, const std::string& projectIdFilter) {
    // Get all tasks for the agent, optionally filtered by project
    std::vector<std::string> projectIds;

    if (projectIdFilter.empty()) {
        auto projects = ListProjects(agentId, "active");
        for (const auto& p : projects) projectIds.push_back(p.id);
    } else {
        projectIds.push_back(projectIdFilter);
    }

    for (const auto& pid : projectIds) {
        auto tasks = ListTasks(agentId, pid);

        // Check if any task is already in_progress
        bool hasInProgress = false;
        for (const auto& t : tasks) {
            if (t.status == "in_progress") { hasInProgress = true; break; }
        }
        if (hasInProgress) continue;

        // Find first ready task (all deps completed)
        for (const auto& t : tasks) {
            if (t.status != "pending" && t.status != "ready") continue;

            bool allDepsComplete = true;
            for (const auto& depId : t.depends_on) {
                auto depIt = std::find_if(tasks.begin(), tasks.end(),
                    [&](const ProjectTask& x) { return x.id == depId; });
                if (depIt == tasks.end() || depIt->status != "completed") {
                    allDepsComplete = false;
                    break;
                }
            }
            if (allDepsComplete) return t;
        }
    }
    return std::nullopt;
}

// ============================================================================
// Cycle detection
// ============================================================================

bool ProjectStore::WouldCreateCycle(const std::string& agentId, const std::string& projectId,
                                     const std::string& taskId, const std::string& dependsOnTaskId) {
    if (taskId == dependsOnTaskId) return true;

    // BFS from dependsOnTaskId — if we reach taskId, adding this edge creates a cycle
    auto tasks = ListTasks(agentId, projectId);

    std::set<std::string> visited;
    std::vector<std::string> queue{dependsOnTaskId};

    while (!queue.empty()) {
        std::string current = queue.back();
        queue.pop_back();

        if (current == taskId) return true;
        if (visited.count(current)) continue;
        visited.insert(current);

        // Find current task's dependencies to traverse
        for (const auto& t : tasks) {
            if (t.id == current) {
                for (const auto& dep : t.depends_on) {
                    if (!visited.count(dep)) queue.push_back(dep);
                }
                break;
            }
        }
    }
    return false;
}

// ============================================================================
// ProjectTool
// ============================================================================

ProjectTool::ProjectTool(ProjectStore* store)
    : m_store(store) {}

ToolDefinition ProjectTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "project";
    def.description =
        "Manage persistent projects and tasks across sessions. "
        "Create projects, add tasks with dependencies, track progress, "
        "and find the next ready task to work on. "
        "Projects survive session boundaries — use them for multi-step objectives.";
    def.resultMode = ToolResultMode::deliver_to_model;

    def.parameters.push_back({
        "action", "string",
        "Operation: create, add_task, list, tasks, next_ready, update, complete, progress, abandon",
        true, "", {"create", "add_task", "list", "tasks", "next_ready", "update", "complete", "progress", "abandon"}
    });

    def.parameters.push_back({"project_id", "string", "Project ID (required for most actions)", false});
    def.parameters.push_back({"task_id", "string", "Task ID (for update, complete)", false});
    def.parameters.push_back({"title", "string", "Title for project or task", false});
    def.parameters.push_back({"description", "string", "Project description (for create)", false});
    def.parameters.push_back({"detail", "string", "Task detail/notes (for add_task, update)", false});
    def.parameters.push_back({"status", "string", "Task status (for update, tasks filter)", false});
    def.parameters.push_back({"result", "string", "Completion result or output (for complete, update)", false});
    def.parameters.push_back({"depends_on", "array", "JSON array of task IDs this task depends on (for add_task, update)", false});

    return def;
}

ToolResult ProjectTool::Execute(const ToolCall& call) {
    ToolResult result;

    Json::Value args;
    Json::CharReaderBuilder builder;
    auto* reader = builder.newCharReader();
    reader->parse(call.arguments.c_str(), call.arguments.c_str() + call.arguments.size(), &args, nullptr);
    delete reader;

    const std::string agentId = args.get("__agent_id", "").asString();
    if (agentId.empty()) {
        result.success = false;
        result.error = "Agent ID is required (injected by ChainRunner)";
        return result;
    }

    const std::string action = args.get("action", "").asString();

    if (action == "create") return HandleCreate(agentId, args);
    if (action == "add_task") return HandleAddTask(agentId, args);
    if (action == "list") return HandleList(agentId, args);
    if (action == "tasks") return HandleTasks(agentId, args);
    if (action == "next_ready") return HandleNextReady(agentId, args);
    if (action == "update") return HandleUpdate(agentId, args);
    if (action == "complete") return HandleComplete(agentId, args);
    if (action == "progress") return HandleProgress(agentId, args);
    if (action == "abandon") return HandleAbandon(agentId, args);

    result.success = false;
    result.error = "Unknown action: " + action;
    return result;
}

// ============================================================================
// Action handlers
// ============================================================================

ToolResult ProjectTool::HandleCreate(const std::string& agentId, const Json::Value& args) {
    ToolResult result;
    std::string title = args.get("title", "").asString();
    std::string desc = args.get("description", "").asString();

    if (title.empty()) {
        result.success = false;
        result.error = "title is required";
        return result;
    }

    auto p = m_store->CreateProject(agentId, title, desc);
    result.success = true;

    Json::Value out = ProjectToJson(p);
    out["message"] = "Project created: " + title;
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    result.output = Json::writeString(wb, out);
    return result;
}

ToolResult ProjectTool::HandleAddTask(const std::string& agentId, const Json::Value& args) {
    ToolResult result;
    std::string projectId = args.get("project_id", "").asString();
    std::string title = args.get("title", "").asString();
    std::string detail = args.get("detail", "").asString();

    if (projectId.empty() || title.empty()) {
        result.success = false;
        result.error = "project_id and title are required";
        return result;
    }

    // Verify project exists and belongs to agent
    auto project = m_store->GetProject(agentId, projectId);
    if (!project) {
        result.success = false;
        result.error = "Project not found: " + projectId;
        return result;
    }

    // Parse dependencies
    std::vector<std::string> dependsOn;
    if (args.isMember("depends_on") && args["depends_on"].isArray()) {
        for (const auto& d : args["depends_on"]) dependsOn.push_back(d.asString());
    }

    // Cycle detection: for each dependency, check if adding task→dep would create a cycle
    // Since we're creating a new task, the task ID doesn't exist yet, so we only need
    // to verify the referenced tasks exist
    auto existingTasks = m_store->ListTasks(agentId, projectId);
    for (const auto& depId : dependsOn) {
        bool found = false;
        for (const auto& t : existingTasks) {
            if (t.id == depId) { found = true; break; }
        }
        if (!found) {
            result.success = false;
            result.error = "Dependency task not found: " + depId;
            return result;
        }
    }

    auto task = m_store->AddTask(agentId, projectId, title, detail, dependsOn);
    result.success = true;

    Json::Value out = TaskToJson(task);
    out["message"] = "Task added: " + title;
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    result.output = Json::writeString(wb, out);
    return result;
}

ToolResult ProjectTool::HandleList(const std::string& agentId, const Json::Value& args) {
    ToolResult result;
    std::string statusFilter = args.get("status", "").asString();

    auto projects = m_store->ListProjects(agentId, statusFilter);

    Json::Value out(Json::objectValue);
    out["projects"] = Json::Value(Json::arrayValue);

    for (const auto& p : projects) {
        auto tasks = m_store->ListTasks(agentId, p.id);
        int completed = 0;
        for (const auto& t : tasks) {
            if (t.status == "completed") completed++;
        }

        Json::Value pj = ProjectToJson(p, static_cast<int>(tasks.size()));
        pj["completed_tasks"] = completed;
        out["projects"].append(pj);
    }

    out["total"] = static_cast<Json::Int>(projects.size());

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    result.success = true;
    result.output = Json::writeString(wb, out);
    return result;
}

ToolResult ProjectTool::HandleTasks(const std::string& agentId, const Json::Value& args) {
    ToolResult result;
    std::string projectId = args.get("project_id", "").asString();
    std::string statusFilter = args.get("status", "").asString();

    if (projectId.empty()) {
        result.success = false;
        result.error = "project_id is required";
        return result;
    }

    auto tasks = m_store->ListTasks(agentId, projectId, statusFilter);

    Json::Value out(Json::objectValue);
    out["tasks"] = Json::Value(Json::arrayValue);
    for (const auto& t : tasks) {
        out["tasks"].append(TaskToJson(t));
    }
    out["total"] = static_cast<Json::Int>(tasks.size());

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    result.success = true;
    result.output = Json::writeString(wb, out);
    return result;
}

ToolResult ProjectTool::HandleNextReady(const std::string& agentId, const Json::Value& args) {
    ToolResult result;
    std::string projectId = args.get("project_id", "").asString();

    auto task = m_store->NextReady(agentId, projectId);
    if (!task) {
        result.success = true;
        Json::Value out(Json::objectValue);
        out["task"] = Json::nullValue;
        out["message"] = "No ready tasks available";
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        result.output = Json::writeString(wb, out);
        return result;
    }

    result.success = true;
    Json::Value out(Json::objectValue);
    out["task"] = TaskToJson(*task);

    // Mark as in_progress automatically
    ProjectTask updated = *task;
    updated.status = "in_progress";
    m_store->UpdateTask(agentId, updated);
    out["status"] = "marked in_progress";

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    result.output = Json::writeString(wb, out);
    return result;
}

ToolResult ProjectTool::HandleUpdate(const std::string& agentId, const Json::Value& args) {
    ToolResult result;
    std::string taskId = args.get("task_id", "").asString();

    if (taskId.empty()) {
        result.success = false;
        result.error = "task_id is required";
        return result;
    }

    auto task = m_store->GetTask(agentId, taskId);
    if (!task) {
        result.success = false;
        result.error = "Task not found: " + taskId;
        return result;
    }

    if (args.isMember("title")) task->title = args["title"].asString();
    if (args.isMember("detail")) task->detail = args["detail"].asString();
    if (args.isMember("status")) task->status = args["status"].asString();
    if (args.isMember("result")) task->result = args["result"].asString();

    // Update dependencies if provided — validate each dep exists and check for cycles
    if (args.isMember("depends_on") && args["depends_on"].isArray()) {
        std::vector<std::string> newDeps;
        for (const auto& d : args["depends_on"]) newDeps.push_back(d.asString());

        // Verify all dependency tasks exist in the same project
        auto allTasks = m_store->ListTasks(agentId, task->project_id);
        for (const auto& depId : newDeps) {
            bool found = false;
            for (const auto& t : allTasks) {
                if (t.id == depId) { found = true; break; }
            }
            if (!found) {
                result.success = false;
                result.error = "Dependency task not found: " + depId;
                return result;
            }
            // Cycle check: adding task→dep must not create a cycle
            if (m_store->WouldCreateCycle(agentId, task->project_id, task->id, depId)) {
                result.success = false;
                result.error = "Adding dependency on " + depId + " would create a cycle";
                return result;
            }
        }
        task->depends_on = newDeps;
    }

    m_store->UpdateTask(agentId, *task);

    result.success = true;
    Json::Value out = TaskToJson(*task);
    out["message"] = "Task updated";
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    result.output = Json::writeString(wb, out);
    return result;
}

ToolResult ProjectTool::HandleComplete(const std::string& agentId, const Json::Value& args) {
    ToolResult result;
    std::string taskId = args.get("task_id", "").asString();
    std::string taskResult = args.get("result", "").asString();

    if (taskId.empty()) {
        result.success = false;
        result.error = "task_id is required";
        return result;
    }

    auto task = m_store->GetTask(agentId, taskId);
    if (!task) {
        result.success = false;
        result.error = "Task not found: " + taskId;
        return result;
    }

    task->status = "completed";
    if (!taskResult.empty()) task->result = taskResult;
    m_store->UpdateTask(agentId, *task);

    // Find newly unblocked tasks
    auto allTasks = m_store->ListTasks(agentId, task->project_id);
    std::vector<std::string> unblocked;
    for (const auto& t : allTasks) {
        if (t.id == taskId) continue;
        if (t.status != "pending") continue;

        bool allDepsComplete = true;
        for (const auto& depId : t.depends_on) {
            auto depIt = std::find_if(allTasks.begin(), allTasks.end(),
                [&](const ProjectTask& x) { return x.id == depId; });
            if (depIt == allTasks.end() || depIt->status != "completed") {
                allDepsComplete = false;
                break;
            }
        }
        if (allDepsComplete) {
            // Mark as ready
            ProjectTask updated = t;
            updated.status = "ready";
            m_store->UpdateTask(agentId, updated);
            unblocked.push_back(t.title + " (id: " + t.id + ")");
        }
    }

    result.success = true;
    Json::Value out(Json::objectValue);
    out["task_id"] = taskId;
    out["status"] = "completed";
    if (!unblocked.empty()) {
        out["unblocked"] = Json::Value(Json::arrayValue);
        for (const auto& u : unblocked) out["unblocked"].append(u);
    }
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    result.output = Json::writeString(wb, out);
    return result;
}

ToolResult ProjectTool::HandleProgress(const std::string& agentId, const Json::Value& args) {
    ToolResult result;
    std::string projectId = args.get("project_id", "").asString();

    if (projectId.empty()) {
        result.success = false;
        result.error = "project_id is required";
        return result;
    }

    auto project = m_store->GetProject(agentId, projectId);
    if (!project) {
        result.success = false;
        result.error = "Project not found: " + projectId;
        return result;
    }

    auto tasks = m_store->ListTasks(agentId, projectId);
    std::string progress = FormatProgress(tasks);

    result.success = true;
    Json::Value out(Json::objectValue);
    out["project"] = project->title;
    out["status"] = project->status;
    out["progress"] = progress;

    int completed = 0;
    for (const auto& t : tasks) if (t.status == "completed") completed++;
    out["completed"] = static_cast<Json::Int>(completed);
    out["total_tasks"] = static_cast<Json::Int>(static_cast<int>(tasks.size()));

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    result.output = Json::writeString(wb, out);
    return result;
}

ToolResult ProjectTool::HandleAbandon(const std::string& agentId, const Json::Value& args) {
    ToolResult result;
    std::string projectId = args.get("project_id", "").asString();

    if (projectId.empty()) {
        result.success = false;
        result.error = "project_id is required";
        return result;
    }

    bool ok = m_store->UpdateProjectStatus(agentId, projectId, "abandoned");
    result.success = ok;
    Json::Value out(Json::objectValue);
    out["project_id"] = projectId;
    out["status"] = ok ? "abandoned" : "not found";
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    result.output = Json::writeString(wb, out);
    if (!ok) result.error = "Project not found or already abandoned";
    return result;
}

// ============================================================================
// JSON helpers
// ============================================================================

Json::Value ProjectTool::ProjectToJson(const Project& p, int taskCount) const {
    Json::Value out(Json::objectValue);
    out["id"] = p.id;
    out["title"] = p.title;
    out["description"] = p.description;
    out["status"] = p.status;
    if (taskCount >= 0) out["task_count"] = taskCount;
    return out;
}

Json::Value ProjectTool::TaskToJson(const ProjectTask& t) const {
    Json::Value out(Json::objectValue);
    out["id"] = t.id;
    out["project_id"] = t.project_id;
    out["title"] = t.title;
    out["detail"] = t.detail;
    out["status"] = t.status;

    Json::Value deps(Json::arrayValue);
    for (const auto& d : t.depends_on) deps.append(d);
    out["depends_on"] = deps;

    out["position"] = t.position;
    if (!t.result.empty()) out["result"] = t.result;
    return out;
}

std::string ProjectTool::FormatProgress(const std::vector<ProjectTask>& tasks) const {
    if (tasks.empty()) return "(no tasks)";

    std::ostringstream oss;

    // Status symbols
    auto statusSymbol = [](const std::string& status) -> std::string {
        if (status == "completed") return "✅";
        if (status == "in_progress") return "🔄";
        if (status == "ready") return "🎯";
        if (status == "failed") return "❌";
        return "⬜"; // pending
    };

    // Build a map of task ID → task for dependency lookups
    std::map<std::string, const ProjectTask*> taskMap;
    for (const auto& t : tasks) taskMap[t.id] = &t;

    // Find root tasks (no dependencies) and dependent tasks
    std::set<std::string> allDepIds;
    for (const auto& t : tasks) {
        for (const auto& d : t.depends_on) allDepIds.insert(d);
    }

    // Print tasks in position order with indentation based on dependency depth
    for (size_t i = 0; i < tasks.size(); ++i) {
        const auto& t = tasks[i];
        if (i > 0) oss << "\n";

        oss << statusSymbol(t.status) << " " << t.title;
        oss << " (" << t.status;
        if (!t.result.empty()) oss << ": " << t.result.substr(0, 60);
        oss << ")";

        if (!t.depends_on.empty()) {
            oss << " ← depends on: ";
            for (size_t j = 0; j < t.depends_on.size(); ++j) {
                if (j > 0) oss << ", ";
                auto depIt = taskMap.find(t.depends_on[j]);
                if (depIt != taskMap.end()) {
                    oss << depIt->second->title;
                } else {
                    oss << t.depends_on[j].substr(0, 8);
                }
            }
        }
    }

    return oss.str();
}

} // namespace animus::kernel

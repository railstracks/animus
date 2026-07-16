#pragma once

#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/tools/ToolTypes.h"

#include <json/json.h>
#include <string>
#include <vector>
#include <optional>
#include <map>

namespace animus::kernel {

class IDataStore;

// ============================================================================
// ProjectStore — persistence for projects and tasks
// ============================================================================

struct Project {
    std::string id;
    std::string agent_id;
    std::string title;
    std::string description;
    std::string status{"active"};  // active, completed, paused, abandoned
    int64_t created_at{0};
    int64_t updated_at{0};
};

struct ProjectTask {
    std::string id;
    std::string project_id;
    std::string title;
    std::string detail;
    std::string status{"pending"};  // pending, ready, in_progress, completed, failed
    std::vector<std::string> depends_on;
    int position{0};
    std::string result;
    int64_t created_at{0};
    int64_t updated_at{0};
};

class ProjectStore {
public:
    explicit ProjectStore(IDataStore* store);

    // Schema
    void EnsureSchema();

    // Projects
    Project CreateProject(const std::string& agentId, const std::string& title, const std::string& description);
    std::vector<Project> ListProjects(const std::string& agentId, const std::string& statusFilter = "");
    std::optional<Project> GetProject(const std::string& agentId, const std::string& projectId);
    bool UpdateProjectStatus(const std::string& agentId, const std::string& projectId, const std::string& status);

    // Tasks
    ProjectTask AddTask(const std::string& agentId, const std::string& projectId,
                        const std::string& title, const std::string& detail,
                        const std::vector<std::string>& dependsOn);
    std::vector<ProjectTask> ListTasks(const std::string& agentId, const std::string& projectId,
                                       const std::string& statusFilter = "");
    std::optional<ProjectTask> GetTask(const std::string& agentId, const std::string& taskId);
    bool UpdateTask(const std::string& agentId, const ProjectTask& task);
    std::optional<ProjectTask> NextReady(const std::string& agentId, const std::string& projectIdFilter = "");

    // Dependency check
    bool WouldCreateCycle(const std::string& agentId, const std::string& projectId,
                          const std::string& taskId, const std::string& dependsOnTaskId);

private:
    IDataStore* m_store;
    std::string GenerateId();
};

// ============================================================================
// ProjectTool — tool interface for agents
// ============================================================================

class ProjectTool : public IToolHandler {
public:
    explicit ProjectTool(ProjectStore* store);

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

private:
    ToolResult HandleCreate(const std::string& agentId, const Json::Value& args);
    ToolResult HandleAddTask(const std::string& agentId, const Json::Value& args);
    ToolResult HandleList(const std::string& agentId, const Json::Value& args);
    ToolResult HandleTasks(const std::string& agentId, const Json::Value& args);
    ToolResult HandleNextReady(const std::string& agentId, const Json::Value& args);
    ToolResult HandleUpdate(const std::string& agentId, const Json::Value& args);
    ToolResult HandleComplete(const std::string& agentId, const Json::Value& args);
    ToolResult HandleProgress(const std::string& agentId, const Json::Value& args);
    ToolResult HandleAbandon(const std::string& agentId, const Json::Value& args);

    Json::Value TaskToJson(const ProjectTask& task) const;
    Json::Value ProjectToJson(const Project& project, int taskCount = -1) const;
    std::string FormatProgress(const std::vector<ProjectTask>& tasks) const;

    ProjectStore* m_store;
};

} // namespace animus::kernel

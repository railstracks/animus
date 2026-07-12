#include "animus_kernel/tools/ShellExecTool.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <thread>

#include <json/json.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

namespace animus::kernel {

namespace {

constexpr int kDefaultTimeout = 30;

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

struct ExecResult {
    std::string stdout_output;
    std::string stderr_output;
    int exit_code{-1};
    bool timed_out{false};
};

// Execute a command and capture stdout/stderr with timeout.
// Uses pipe + fork + exec for portable capture of both streams.
ExecResult ExecuteCommand(const std::string& command, const std::string& workingDir, int timeoutSeconds) {
    ExecResult result;

    // Create pipes for stdout and stderr
    int stdoutPipe[2];
    int stderrPipe[2];
    if (pipe(stdoutPipe) < 0 || pipe(stderrPipe) < 0) {
        result.exit_code = -1;
        result.stderr_output = "Failed to create pipes";
        return result;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdoutPipe[0]); close(stdoutPipe[1]);
        close(stderrPipe[0]); close(stderrPipe[1]);
        result.exit_code = -1;
        result.stderr_output = "Failed to fork";
        return result;
    }

    if (pid == 0) {
        // Child process
        close(stdoutPipe[0]);
        close(stderrPipe[0]);
        dup2(stdoutPipe[1], STDOUT_FILENO);
        dup2(stderrPipe[1], STDERR_FILENO);
        close(stdoutPipe[1]);
        close(stderrPipe[1]);

        // Change working directory if specified
        if (!workingDir.empty()) {
            (void)chdir(workingDir.c_str());
        }

        // Execute via /bin/sh for shell features (pipes, redirects, etc.)
        execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
        _exit(127);
    }

    // Parent process
    close(stdoutPipe[1]);
    close(stderrPipe[1]);

    // Read stdout and stderr with timeout
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(timeoutSeconds > 0 ? timeoutSeconds : 3600);

    auto readAll = [](int fd) -> std::string {
        std::string output;
        char buffer[4096];
        ssize_t n;
        while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
            output.append(buffer, n);
        }
        return output;
    };

    // Simple approach: read stdout, then stderr, check time between
    // For a production tool we'd use poll/select, but this works for reasonable commands.
    bool timedOut = false;

    // Read stdout
    result.stdout_output = readAll(stdoutPipe[0]);
    close(stdoutPipe[0]);

    // Read stderr
    result.stderr_output = readAll(stderrPipe[0]);
    close(stderrPipe[0]);

    // Wait for child with timeout
    int status = 0;
    pid_t ret;
    while ((ret = waitpid(pid, &status, WNOHANG)) == 0) {
        if (std::chrono::steady_clock::now() > deadline) {
            // Kill the process group
            kill(-pid, SIGTERM);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            kill(-pid, SIGKILL);
            waitpid(pid, &status, 0);
            result.timed_out = true;
            timedOut = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (!timedOut) {
        if (WIFEXITED(status)) {
            result.exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            result.exit_code = -WTERMSIG(status);
        }
    }

    return result;
}

} // anonymous namespace

// ============================================================================
// Construction
// ============================================================================

ShellExecTool::ShellExecTool(const std::string& workspaceRoot)
    : m_workspaceRoot(workspaceRoot.empty() ? std::filesystem::current_path().string() : workspaceRoot) {}

// ============================================================================
// Definition
// ============================================================================

ToolDefinition ShellExecTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "shell_exec";
    def.description =
        "Execute a shell command on the current node. Returns stdout, stderr, "
        "and exit code. Commands run in the node's working directory.";
    def.resultMode = ToolResultMode::deliver_to_model;

    ToolParameter commandParam;
    commandParam.name = "command";
    commandParam.type = "string";
    commandParam.description = "Shell command to execute";
    commandParam.required = true;
    def.parameters.push_back(commandParam);

    ToolParameter timeoutParam;
    timeoutParam.name = "timeout";
    timeoutParam.type = "integer";
    timeoutParam.description = "Timeout in seconds (default 30). 0 for no timeout.";
    timeoutParam.required = false;
    def.parameters.push_back(timeoutParam);

    ToolParameter workDirParam;
    workDirParam.name = "working_dir";
    workDirParam.type = "string";
    workDirParam.description = "Working directory override. Defaults to the node's workspace root.";
    workDirParam.required = false;
    def.parameters.push_back(workDirParam);

    return def;
}

// ============================================================================
// Execute
// ============================================================================

ToolResult ShellExecTool::Execute(const ToolCall& call) {
    ToolResult result;
    result.call_id = call.id;

    auto args = ParseArgs(call.arguments);
    if (args.isNull()) {
        result.success = false;
        result.error = "Failed to parse tool arguments as JSON";
        return result;
    }

    std::string command;
    if (args.isMember("command") && args["command"].isString()) {
        command = args["command"].asString();
    }
    if (command.empty()) {
        result.success = false;
        result.error = "Missing required parameter: command";
        return result;
    }

    // Check if shell execution is enabled
    if (!m_enabled) {
        result.success = false;
        result.error = "Shell execution is disabled by security policy";
        return result;
    }

    // Check command against policy
    if (!IsCommandAllowed(command)) {
        result.success = false;
        result.error = "Command blocked by security policy: " + command;
        return result;
    }

    int timeout = kDefaultTimeout;
    if (args.isMember("timeout") && args["timeout"].isInt()) {
        timeout = args["timeout"].asInt();
    }

    // Clamp timeout to maximum allowed
    if (timeout > m_maxTimeout) {
        timeout = m_maxTimeout;
    }

    std::string workingDir = m_workspaceRoot;
    if (args.isMember("working_dir") && args["working_dir"].isString()) {
        workingDir = args["working_dir"].asString();
    }

    // Execute
    auto execResult = ExecuteCommand(command, workingDir, timeout);

    // Build structured output
    Json::Value output(Json::objectValue);
    output["exit_code"] = execResult.exit_code;
    output["timed_out"] = execResult.timed_out;
    output["stdout"] = execResult.stdout_output;
    output["stderr"] = execResult.stderr_output;

    // Truncate very large outputs
    if (output["stdout"].asString().size() > 50000) {
        std::string truncated = output["stdout"].asString();
        truncated.resize(50000);
        truncated += "\n... (truncated)";
        output["stdout"] = truncated;
    }

    Json::StreamWriterBuilder writer;
    result.output = Json::writeString(writer, output);
    result.success = (execResult.exit_code == 0 && !execResult.timed_out);
    if (!result.success && !execResult.stderr_output.empty()) {
        result.error = execResult.stderr_output;
    } else if (execResult.timed_out) {
        result.error = "Command timed out after " + std::to_string(timeout) + " seconds";
    }

    return result;
}


// ============================================================================
// Command policy
// ============================================================================

void ShellExecTool::SetEnabled(bool enabled) {
    m_enabled = enabled;
}

void ShellExecTool::SetCommandAllowlist(const std::vector<std::string>& patterns) {
    m_commandAllowlist = patterns;
}

void ShellExecTool::SetCommandDenylist(const std::vector<std::string>& patterns) {
    m_commandDenylist = patterns;
}

void ShellExecTool::SetMaxTimeout(int maxSeconds) {
    m_maxTimeout = maxSeconds;
}

bool ShellExecTool::IsCommandAllowed(const std::string& command) const {
    // Denylist takes precedence
    for (const auto& pattern : m_commandDenylist) {
        if (GlobMatch(command, pattern)) {
            return false;
        }
    }

    // Allowlist — if non-empty, command must match at least one pattern
    if (!m_commandAllowlist.empty()) {
        bool allowed = false;
        for (const auto& pattern : m_commandAllowlist) {
            if (GlobMatch(command, pattern)) {
                allowed = true;
                break;
            }
        }
        if (!allowed) return false;
    }

    return true;
}

bool ShellExecTool::GlobMatch(const std::string& text, const std::string& pattern) const {
    const auto n = text.size();
    const auto m = pattern.size();
    std::vector<std::vector<bool>> dp(n + 1, std::vector<bool>(m + 1, false));
    dp[0][0] = true;

    for (size_t j = 1; j <= m && pattern[j - 1] == '*'; ++j) {
        dp[0][j] = true;
    }

    for (size_t i = 1; i <= n; ++i) {
        for (size_t j = 1; j <= m; ++j) {
            if (pattern[j - 1] == '*') {
                dp[i][j] = dp[i][j - 1] || dp[i - 1][j];
            } else if (pattern[j - 1] == '?' || pattern[j - 1] == text[i - 1]) {
                dp[i][j] = dp[i - 1][j - 1];
            }
        }
    }

    return dp[n][m];
}

} // namespace animus::kernel

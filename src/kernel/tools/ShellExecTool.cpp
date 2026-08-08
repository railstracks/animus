#include "animus_kernel/tools/ShellExecTool.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <thread>

#include <json/json.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

namespace animus::kernel {

namespace {

constexpr int kDefaultTimeout = 30;
constexpr size_t kMaxOutputBytes = 1 * 1024 * 1024;  // 1 MB cap per stream during read

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
    bool output_truncated{false};
};

// Execute a command and capture stdout/stderr with timeout.
// Uses poll() on non-blocking pipes to drain both streams concurrently
// while independently enforcing the deadline. Creates a process group
// so the entire child subtree can be killed on timeout.
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
        // Child process — create own process group so the parent can
        // kill the entire subtree on timeout.
        setpgid(0, 0);

        close(stdoutPipe[0]);
        close(stderrPipe[0]);
        dup2(stdoutPipe[1], STDOUT_FILENO);
        dup2(stderrPipe[1], STDERR_FILENO);
        close(stdoutPipe[1]);
        close(stderrPipe[1]);

        // Change working directory if specified
        if (!workingDir.empty()) {
            if (chdir(workingDir.c_str()) != 0) {
                _exit(126);
            }
        }

        // Execute via /bin/sh for shell features (pipes, redirects, etc.)
        execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
        // exec failed
        _exit(127);
    }

    // Parent process
    close(stdoutPipe[1]);
    close(stderrPipe[1]);

    // Set pipes to non-blocking
    int stdoutFd = stdoutPipe[0];
    int stderrFd = stderrPipe[0];
    int stdoutFlags = fcntl(stdoutFd, F_GETFL, 0);
    int stderrFlags = fcntl(stderrFd, F_GETFL, 0);
    if (stdoutFlags >= 0) fcntl(stdoutFd, F_SETFL, stdoutFlags | O_NONBLOCK);
    if (stderrFlags >= 0) fcntl(stderrFd, F_SETFL, stderrFlags | O_NONBLOCK);

    // Deadline for timeout enforcement
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(timeoutSeconds > 0 ? timeoutSeconds : 3600);

    bool stdoutClosed = false;
    bool stderrClosed = false;
    size_t stdoutBytes = 0;
    size_t stderrBytes = 0;

    // Drain both pipes concurrently with poll()
    while (!stdoutClosed || !stderrClosed) {
        // Check deadline
        if (std::chrono::steady_clock::now() >= deadline) {
            result.timed_out = true;
            break;
        }

        // Build pollfd set for open pipes
        struct pollfd pfds[2];
        int nfds = 0;
        int timeoutMs = 200;  // poll timeout — lets us re-check deadline periodically

        if (!stdoutClosed) {
            pfds[nfds].fd = stdoutFd;
            pfds[nfds].events = POLLIN;
            nfds++;
        }
        if (!stderrClosed) {
            pfds[nfds].fd = stderrFd;
            pfds[nfds].events = POLLIN;
            nfds++;
        }

        if (nfds == 0) break;

        int rc = poll(pfds, static_cast<nfds_t>(nfds), timeoutMs);
        if (rc < 0) {
            if (errno == EINTR) continue;
            break;  // poll error
        }
        if (rc == 0) continue;  // timeout — loop back and re-check deadline

        // Drain ready pipes
        for (int i = 0; i < nfds; ++i) {
            if (!(pfds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;

            char buffer[8192];
            ssize_t n = read(pfds[i].fd, buffer, sizeof(buffer));

            if (n > 0) {
                // Check output limits before appending
                if (pfds[i].fd == stdoutFd) {
                    if (stdoutBytes + static_cast<size_t>(n) <= kMaxOutputBytes) {
                        result.stdout_output.append(buffer, n);
                        stdoutBytes += static_cast<size_t>(n);
                    } else {
                        result.output_truncated = true;
                    }
                } else {
                    if (stderrBytes + static_cast<size_t>(n) <= kMaxOutputBytes) {
                        result.stderr_output.append(buffer, n);
                        stderrBytes += static_cast<size_t>(n);
                    } else {
                        result.output_truncated = true;
                    }
                }
            } else if (n == 0 || (n < 0 && errno != EINTR && errno != EAGAIN)) {
                // Pipe closed (EOF or error)
                if (pfds[i].fd == stdoutFd) {
                    stdoutClosed = true;
                    close(stdoutFd);
                } else {
                    stderrClosed = true;
                    close(stderrFd);
                }
            }
        }
    }

    // Close any remaining open pipes
    if (!stdoutClosed) close(stdoutFd);
    if (!stderrClosed) close(stderrFd);

    // Handle timeout — kill the entire process group
    if (result.timed_out) {
        // SIGTERM first, brief grace, then SIGKILL
        kill(-pid, SIGTERM);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        kill(-pid, SIGKILL);
        waitpid(pid, nullptr, 0);
    } else {
        // Wait for child to exit (should be quick — pipes are drained)
        int status = 0;
        // Poll with deadline in case waitpid hangs (shouldn't, but defensive)
        while (true) {
            pid_t ret = waitpid(pid, &status, WNOHANG);
            if (ret == pid) break;
            if (ret < 0) break;  // error
            if (std::chrono::steady_clock::now() >= deadline) {
                kill(-pid, SIGKILL);
                waitpid(pid, &status, 0);
                result.timed_out = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (!result.timed_out) {
            if (WIFEXITED(status)) {
                result.exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                result.exit_code = -WTERMSIG(status);
            }
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
    if (execResult.output_truncated) {
        output["output_truncated"] = true;
    }

    // Truncate very large outputs (post-read safety, in addition to the
    // 1 MB cap during read)
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
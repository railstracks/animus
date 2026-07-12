#pragma once

#include "animus_kernel/tools/ToolRegistry.h"

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

struct lua_State;

namespace animus::kernel {

class AgentConfigStore;

class ToolRegistry;
class HttpClient;

// ============================================================================
// LuaPluginInfo — describes a plugin registered through animus.register_*
// ============================================================================

struct LuaPluginInfo {
    std::string id;              // Plugin identifier (e.g. "bluesky", "signal")
    std::string name;            // Human-readable name
    std::string type;            // "tool", "interface", "social"
    int handlerRef;              // Lua registry reference to handler function
    std::string agentId;         // Agent that registered this plugin
    std::unique_ptr<IToolHandler> handler;  // For social: owned here, not in ToolRegistry

    // Composite tool fields (used by interface/social plugins)
    std::vector<std::string> actions;       // e.g. {"send", "list_contacts"}
    std::vector<std::string> capabilities;  // e.g. {"read", "write", "search"}
    std::unordered_map<std::string, std::vector<ToolParameter>> actionSchemas; // action → params
};

// ============================================================================
// LuaState — per-agent Lua VM with sandboxed environment
//
// Each agent gets its own Lua VM. Scripts run in a sandboxed environment
// with restricted stdlib, tool bridge access, and managed HTTP client.
// ============================================================================

class LuaState {
public:
    // Configuration for a Lua VM instance
    struct Config {
        std::string agentId;
        size_t memoryLimitBytes = 64 * 1024 * 1024;  // 64 MB default
        size_t instructionLimit = 10'000'000;          // 10M instructions default
        size_t executionTimeoutMs = 30'000;             // 30 seconds default
        size_t httpRateLimitPerMin = 60;                // 60 HTTP requests/minute default
        AgentConfigStore* configStore = nullptr;         // persistent config (may be null)
    };

    explicit LuaState(const Config& config, ToolRegistry& tools, HttpClient& http);
    ~LuaState();

    LuaState(const LuaState&) = delete;
    LuaState& operator=(const LuaState&) = delete;

    // --- Script execution ---

    /// Execute a Lua script string. Returns the result as a JSON string.
    /// Throws LuaException on runtime errors.
    /// This is the synchronous path — tool calls block until they return.
    std::string Eval(const std::string& script, const std::string& agentId = "default");

    /// Execute a Lua script using coroutines. Tool calls yield back to the caller,
    /// which processes them and resumes. Returns the final result as JSON.
    /// The callback receives each ToolCall and must return a ToolResult.
    /// This is the async-friendly path — tool calls don't block the Lua VM,
    /// allowing interleaved execution.
    using ToolCallCallback = std::function<ToolResult(const ToolCall&)>;
    std::string EvalCoroutine(const std::string& script,
                               const std::string& agentId,
                               ToolCallCallback onToolCall);

    /// Execute a stored script by name. Returns the result as a JSON string.
    std::string RunStored(const std::string& name, const std::string& agentId = "default");

    // --- Plugin registration ---

    /// Get all plugins registered by this VM's scripts.
    const std::vector<LuaPluginInfo>& GetRegisteredPlugins() const;

    /// Access the tool registry (for C callbacks via g_activeState).
    ToolRegistry& GetToolRegistry() { return m_tools; }
    HttpClient& GetHttpClient() { return m_http; }

    /// Whether we're running in coroutine mode (tool calls yield instead of blocking).
    bool IsCoroutineMode() const { return m_coroutineMode; }

    /// Get mutable plugins list (for registration callbacks).
    std::vector<LuaPluginInfo>& GetRegisteredPluginsMutable() { return m_plugins; }

    /// Look up a specific plugin by id and type.
    const LuaPluginInfo* FindPlugin(const std::string& id, const std::string& type) const;

    // --- Script storage ---

    /// Store a script with a name. Overwrites if exists.
    void StoreScript(const std::string& name, const std::string& source);

    /// Retrieve a stored script's source by name. Returns empty if not found.
    std::string GetScript(const std::string& name) const;

    /// List all stored script names.
    std::vector<std::string> ListScripts() const;

    /// Delete a stored script. Returns true if found and deleted.
    bool DeleteScript(const std::string& name);

    // --- Lua registry access ---

    /// Push a Lua value from the registry onto the stack. Returns false if ref invalid.
    bool PushRegistryRef(int ref);

    /// Get the raw lua_State pointer (for LuaToolHandler proxy calls).
    lua_State* GetRawState() const { return m_L; }

    /// Set this LuaState as the thread-local active state.
    /// Must be paired with ClearActiveState() after use.
    /// Required before LuaToolHandler can invoke handlers that
    /// depend on config.get, animus.http_*, log.*, etc.
    void SetActiveState() const;

    /// Clear the thread-local active state.
    static void ClearActiveState();

    // --- Agent config access ---

    /// Get a config value. Returns empty string if not found.
    std::string GetConfig(const std::string& key) const;

    /// Set a config value (agent-scoped, persisted).
    void SetConfig(const std::string& key, const std::string& value);

    /// Get all config keys for this agent.
    std::vector<std::string> ListConfigKeys() const;

private:
    // Setup the sandboxed Lua environment (restricted stdlib, bridge APIs)
    void SetupSandbox();
    void SetupToolBridge();
    void SetupHttpClient();
    void SetupPluginRegistration();
    void SetupConfigAccess();
    void SetupJsonUtil();
    void SetupLogging();

    lua_State* m_L;
    Config m_config;
    ToolRegistry& m_tools;
    HttpClient& m_http;

    // Stored scripts (name → source)
    std::unordered_map<std::string, std::string> m_storedScripts;

    // Registered plugins
    std::vector<LuaPluginInfo> m_plugins;

    // Agent config (key → value)
    // When m_configStore is set, reads/writes go through the persistent store.
    // The in-memory map serves as fallback when no store is configured.
    std::unordered_map<std::string, std::string> m_agentConfig;
    AgentConfigStore* m_configStore;

    // Lua registry references to cleanup
    std::vector<int> m_registryRefs;

    // Coroutine mode flag — when true, tool calls yield instead of blocking
    bool m_coroutineMode{false};

    // Memory allocator context for limit enforcement (defined in .cpp)
    struct AllocCtx;
    AllocCtx* m_allocCtx{nullptr};
};

// ============================================================================
// LuaException — thrown on Lua runtime errors
// ============================================================================

class LuaException : public std::runtime_error {
public:
    explicit LuaException(const std::string& msg) : std::runtime_error(msg) {}
};

} // namespace animus::kernel
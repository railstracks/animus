# Animus — Class Diagram

This document outlines the core C++ classes and their relationships.

## Layer Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                         APPLICATION LAYER                            │
│  (animusd daemon, CLI tools, admin utilities)                        │
└─────────────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────────┐
│                           KERNEL LAYER                               │
│                                                                      │
│  ┌─────────────┐  ┌──────────────┐  ┌───────────────┐               │
│  │   Kernel    │──│SessionManager│──│ MemoryManager │               │
│  └─────────────┘  └──────────────┘  └───────────────┘               │
│         │                                                       │
│         │         ┌──────────────┐  ┌───────────────┐             │
│         └─────────│  JobSystem   │──│PromptCompiler │             │
│                   └──────────────┘  └───────────────┘             │
│                                           │                        │
│                   ┌──────────────┐        │                        │
│                   │  GateKeeper  │◀───────┘                        │
│                   └──────────────┘                                 │
└─────────────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         PROVIDER LAYER                               │
│                                                                      │
│  ┌───────────────────┐                                              │
│  │ IProviderAdapter  │ (interface)                                  │
│  └───────────────────┘                                              │
│            △                                                         │
│            │                                                         │
│   ┌────────┴────────┬──────────────┬──────────────┐                │
│   │                 │              │              │                 │
│ ┌─▼──────┐    ┌─────▼─────┐  ┌─────▼─────┐  ┌─────▼─────┐          │
│ │OpenAI  │    │  Mistral  │  │   Cohere  │  │   GLM     │          │
│ │Adapter │    │  Adapter  │  │  Adapter  │  │  Adapter  │          │
│ └────────┘    └───────────┘  └───────────┘  └───────────┘          │
└─────────────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         CONNECTOR LAYER                              │
│                                                                      │
│  ┌───────────────────┐                                              │
│  │   IConnector      │ (interface)                                  │
│  └───────────────────┘                                              │
│            △                                                         │
│            │                                                         │
│   ┌────────┴────────┬──────────────┐                                │
│   │                 │              │                                │
│ ┌─▼──────┐    ┌─────▼─────┐  ┌─────▼─────┐                          │
│ │ Slack  │    │    Web    │  │    CLI    │                          │
│ │Connector│   │ Connector │  │ Connector │                          │
│ └────────┘    └───────────┘  └───────────┘                          │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Core Classes

### Kernel

The central orchestrator. Owns the cognition loop and coordinates all subsystems.

```cpp
class Kernel {
public:
    Kernel(const KernelConfig& config);
    ~Kernel();

    // Lifecycle
    bool Start();
    void Stop();

    // Session management
    Session* CreateSession(const SessionConfig& config);
    void CloseSession(SessionId id);
    Session* GetSession(SessionId id);

    // Main cognition loop (called per-session)
    CognitionResult RunCognitionLoop(Session& session);

    // Subsystem access
    JobSystem& GetJobSystem();
    MemoryManager& GetMemoryManager();
    ProviderRegistry& GetProviderRegistry();
    GateKeeper& GetGateKeeper();

private:
    std::unique_ptr<Impl> m_impl;
};
```

**Responsibilities:**
- Own the main cognition loop (perceive → retrieve → propose → gate → act → reflect)
- Coordinate JobSystem, MemoryManager, ProviderRegistry
- Enforce global policies (timeouts, budgets)
- Route incoming messages from connectors to sessions

---

### Session

Represents an isolated conversational context.

```cpp
class Session {
public:
    SessionId GetId() const;
    UserId GetUserId() const;
    ConnectorType GetConnector() const;

    // Message history
    void AddMessage(const Message& msg);
    std::vector<Message> GetRecentMessages(size_t count) const;

    // Working memory
    WorkingSet& GetWorkingSet();
    const WorkingSet& GetWorkingSet() const;

    // Context budget
    void SetContextBudget(int tokens);
    int GetContextBudget() const;
    int GetTokensUsed() const;

    // State
    SessionState GetState() const;
    void SetState(SessionState state);

    // Associated jobs
    void AttachJob(JobHandle job);
    void CancelAllJobs();

private:
    friend class Kernel;
    Session(SessionId id, const SessionConfig& config);
    std::unique_ptr<Impl> m_impl;
};
```

---

### SessionManager

Manages the lifecycle and isolation of sessions.

```cpp
class SessionManager {
public:
    SessionManager();

    Session* Create(const SessionConfig& config);
    Session* Get(SessionId id);
    void Close(SessionId id);
    void CloseAll();

    // Query
    std::vector<Session*> GetActiveSessions() const;
    std::vector<Session*> GetSessionsForUser(UserId userId) const;
    size_t ActiveCount() const;

private:
    std::unique_ptr<Impl> m_impl;
};
```

---

### JobSystem

Thread pool with dependency scheduling (ported from Luminal).

```cpp
namespace jobs {

class JobSystem {
public:
    JobSystem();
    ~JobSystem();

    bool Start(const JobSystemConfig& config);
    void Stop(JobSystemStopMode mode = JobSystemStopMode::Drain);

    // Scheduling
    JobHandle Enqueue(Job job);
    JobHandle EnqueueAfter(const std::vector<JobHandle>& deps, Job job);
    JobHandle EnqueueFanIn(const TaskGroup& group, Job job);
    JobHandle EnqueueBarrier(const TaskGroup& group);

    // Waiting
    void Wait(const JobHandle& handle) const;
    void Wait(const TaskGroup& group) const;
    void WaitIdle() const;

    // Stats
    uint32_t WorkerCount() const;
    uint64_t SubmittedJobs() const;
    uint64_t CompletedJobs() const;

    // Diagnostics
    TaskGraphDiagnostics DiagnoseCycles(const TaskGroup& group) const;
};

} // namespace jobs
```

**Animus Extensions (planned):**

```cpp
// Priority-aware scheduling
JobHandle EnqueueWithPriority(Job job, JobPriority priority);

// Session-scoped cancellation
void CancelSessionJobs(SessionId sessionId);

// Named lanes for different work types
enum class JobLane { Cognition, ToolCall, Memory, Hook };
JobHandle EnqueueInLane(JobLane lane, Job job);
```

---

### MemoryManager

Coordinates access to memory lanes.

```cpp
class MemoryManager {
public:
    MemoryManager(const MemoryConfig& config);

    // Event log (append-only)
    void LogEvent(const Event& event);
    std::vector<Event> QueryEvents(const EventQuery& query) const;

    // Working set (per-session)
    WorkingSet& GetWorkingSet(SessionId sessionId);
    void PromoteToFacts(SessionId sessionId, const std::vector<std::string>& keys);

    // Semantic memory (curated facts)
    void StoreFact(const Fact& fact);
    std::vector<Fact> RetrieveFacts(const RetrievalQuery& query) const;
    void SupersedeFact(FactId oldId, FactId newId);

    // Budget enforcement
    void EnforceBudgets();

private:
    std::unique_ptr<Impl> m_impl;
};
```

---

### WorkingSet

Short-term active context for a session.

```cpp
class WorkingSet {
public:
    void Set(const std::string& key, const json& value, int priority = 0);
    json Get(const std::string& key) const;
    bool Has(const std::string& key) const;
    void Remove(const std::string& key);

    // Bulk operations
    std::map<std::string, json> GetAll() const;
    void Clear();

    // Eviction
    void EnforceBudget(int maxItems);  // Evicts by LRU + priority

private:
    std::unique_ptr<Impl> m_impl;
};
```

---

### PromptCompiler

Assembles prompts with policy-driven context selection.

```cpp
class PromptCompiler {
public:
    PromptCompiler(const PromptConfig& config);

    // Compile a prompt for a given category
    CompiledPrompt Compile(
        PromptCategory category,
        const Session& session,
        const Context& context);

    // Category configuration
    void SetCategoryConfig(PromptCategory category, const CategoryConfig& config);
    CategoryConfig GetCategoryConfig(PromptCategory category) const;

private:
    std::unique_ptr<Impl> m_impl;
};

struct CompiledPrompt {
    std::string content;
    int tokenCount;
    PromptCategory category;
    std::string selectedProvider;
    std::string selectedModel;
    std::vector<std::string> retrievedFacts;  // For traceability
};
```

---

### GateKeeper

Enforces safety and policy gates before actions.

```cpp
class GateKeeper {
public:
    GateKeeper(const GateConfig& config);

    // Check if an action is allowed
    GateDecision Evaluate(const Action& action, const Session& session);

    // Policy configuration
    void AddRule(const GateRule& rule);
    void RemoveRule(const std::string& ruleId);

    // Ask-first mode
    void SetAskFirstEnabled(bool enabled);
    bool IsAskFirstEnabled() const;

private:
    std::unique_ptr<Impl> m_impl;
};

enum class GateDecision {
    Allowed,
    Denied,
    AskUser,      // Requires explicit user approval
    LogOnly       // Allowed but must be logged
};
```

---

### HookExecutor

Runs external hooks (Node/Python/Ruby) with strict constraints.

```cpp
class HookExecutor {
public:
    HookExecutor(const HookConfig& config);

    // Execute a hook with structured I/O
    HookResult Execute(const HookCall& call);

    // Constraints
    void SetDefaultTimeout(std::chrono::milliseconds timeout);
    void SetMaxOutputSize(size_t bytes);

private:
    std::unique_ptr<Impl> m_impl;
};

struct HookCall {
    std::string hookPath;
    json input;           // Passed via stdin
    std::chrono::milliseconds timeout;
};

struct HookResult {
    int exitCode;
    json output;          // Parsed from stdout
    std::string stderrOutput;
    std::chrono::milliseconds duration;
    bool timedOut;
};
```

---

## Provider Layer

### IProviderAdapter (Interface)

DLL-loaded provider interface.

```cpp
class IProviderAdapter {
public:
    virtual ~IProviderAdapter() = default;

    // Lifecycle
    virtual bool Initialize(const json& config) = 0;
    virtual void Shutdown() = 0;

    // Capability discovery
    virtual ProviderCapabilities GetCapabilities() const = 0;

    // Synchronous request
    virtual ProviderResponse Complete(const ProviderRequest& request) = 0;

    // Streaming request
    virtual void CompleteStream(
        const ProviderRequest& request,
        std::function<void(const ProviderResponseChunk&)> onChunk) = 0;

    // Thread safety marker
    virtual bool IsThreadSafe() const = 0;
};

struct ProviderCapabilities {
    bool supportsToolCalling;
    bool supportsJsonMode;
    bool supportsStreaming;
    int maxContextTokens;
    std::vector<std::string> availableModels;
};
```

---

### ProviderRegistry

Manages loaded provider DLLs.

```cpp
class ProviderRegistry {
public:
    ProviderRegistry();

    // Load/unload
    bool LoadProvider(const std::string& id, const std::string& dllPath);
    void UnloadProvider(const std::string& id);

    // Access
    IProviderAdapter* GetProvider(const std::string& id) const;
    std::vector<std::string> GetAvailableProviders() const;

    // Selection by category
    std::string SelectProviderForCategory(PromptCategory category) const;

private:
    std::unique_ptr<Impl> m_impl;
};
```

---

## Connector Layer

### IConnector (Interface)

Base interface for external interfaces.

```cpp
class IConnector {
public:
    virtual ~IConnector() = default;

    // Lifecycle
    virtual bool Start(const json& config) = 0;
    virtual void Stop() = 0;

    // Message routing
    virtual void SendMessage(SessionId sessionId, const Message& msg) = 0;

    // Capabilities
    virtual ConnectorType GetType() const = 0;
    virtual std::string GetId() const = 0;
};
```

---

### SlackConnector

```cpp
class SlackConnector : public IConnector {
public:
    bool Start(const json& config) override;
    void Stop() override;
    void SendMessage(SessionId sessionId, const Message& msg) override;
    ConnectorType GetType() const override { return ConnectorType::Slack; }
    std::string GetId() const override { return "slack"; }

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
```

---

## Class Relationship Summary

```
Kernel
  │
  ├─── owns ───▶ SessionManager ───▶ Session
  │                                      │
  │                                      ├─── WorkingSet
  │                                      └─── std::vector<JobHandle>
  │
  ├─── owns ───▶ JobSystem (from Luminal)
  │
  ├─── owns ───▶ MemoryManager
  │                   │
  │                   ├─── EventLog
  │                   ├─── WorkingSet (per Session)
  │                   └─── SemanticMemory
  │
  ├─── owns ───▶ PromptCompiler
  │                    │
  │                    └─── uses ───▶ MemoryManager
  │
  ├─── owns ───▶ GateKeeper
  │
  ├─── owns ───▶ HookExecutor
  │
  ├─── owns ───▶ ProviderRegistry ───▶ IProviderAdapter (DLL)
  │
  └─── owns ───▶ ConnectorRegistry ───▶ IConnector
```

---

## Thread Safety Notes

| Class | Thread Safety |
|-------|---------------|
| `Kernel` | All public methods are thread-safe |
| `Session` | Thread-safe for read; writes serialized per-session |
| `SessionManager` | Thread-safe |
| `JobSystem` | Fully thread-safe (worker pool) |
| `MemoryManager` | Thread-safe; EventLog append is lock-free |
| `WorkingSet` | Thread-safe per session |
| `PromptCompiler` | Thread-safe (immutable config after init) |
| `GateKeeper` | Thread-safe |
| `HookExecutor` | Thread-safe (spawns isolated processes) |
| `ProviderRegistry` | Thread-safe |
| `IProviderAdapter` | Must be thread-safe if `IsThreadSafe()` returns true |

---

## Implementation Notes

1. **PIMPL idiom**: All classes use `std::unique_ptr<Impl>` to hide implementation details and maintain ABI stability.

2. **Dependency injection**: Subsystems (JobSystem, MemoryManager, etc.) are passed to Kernel via config or constructed internally with injectable interfaces for testing.

3. **RAII**: All resource ownership is managed via RAII; destructors perform cleanup.

4. **Error handling**: Methods return `bool` or `std::optional` for fallible operations; exceptions reserved for truly exceptional conditions.

5. **Logging**: All classes have access to a centralized logging interface (injected or global).

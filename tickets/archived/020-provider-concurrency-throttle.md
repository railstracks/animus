# Ticket 020: Provider Concurrency Throttle

## Summary

Add a configurable concurrency limit per provider, backed by a queue that ensures no more than `N` simultaneous LLM connections are active at any time. Excess requests wait their turn in FIFO order.

## Background

Currently every chat message triggers an immediate LLM call through the job system. If multiple messages arrive in quick succession (or multiple sessions are active), all calls fire in parallel with no regard for provider rate limits.

Most providers enforce concurrency limits:
- **Per-provider limits** (all models share one pool): Ollama, Cohere, Mistral
- **Per-model limits** (each model has its own pool): Zai, OpenAI, Anthropic

This ticket implements **per-provider** concurrency limiting. Per-model limiting will be added later when the Model entity is introduced, layered on top of the same throttle infrastructure.

## Architecture

### ProviderThrottle

A concurrency manager that lives alongside the provider state. Each provider gets its own slot pool.

```cpp
class ProviderThrottle {
public:
    explicit ProviderThrottle(std::function<int(const std::string&)> concurrencyLookup);

    // Acquire an execution slot for the given provider.
    // Blocks (via condition variable) if all slots are occupied.
    // Returns a RAII guard that releases the slot on destruction.
    //
    // If the provider's concurrency is 0, returns immediately (unlimited).
    SlotGuard Acquire(const std::string& providerId);

private:
    struct SlotPool {
        int concurrency{1};
        int active{0};
        std::queue<std::promise<void>> waiting;
    };

    std::function<int(const std::string&)> m_lookup;
    std::mutex m_mutex;
    std::unordered_map<std::string, SlotPool> m_pools;
};
```

### SlotGuard (RAII)

```cpp
class SlotGuard {
public:
    ~SlotGuard(); // releases the slot, wakes next waiter
    SlotGuard(SlotGuard&&) noexcept;
    SlotGuard(const SlotGuard&) = delete;
private:
    ProviderThrottle* m_throttle;
    std::string m_providerId;
    bool m_released{false};
};
```

### Integration points

The throttle sits **between** the job system and the chain runner:

1. **WebSocket chat handler** (`AdminServer.cpp`): The Cognition lane job currently calls `ExecuteStreamingOnSession` directly. After this ticket, it acquires a slot first, then executes, and the slot is released when the job completes (success or error).

2. **Future: Consolidation pipeline**: Will also acquire slots for LLM-powered consolidation calls.

3. **Future: Model entity**: When models become first-class, `Acquire` gains an optional model parameter. Per-model limits use a separate pool keyed by `provider:model`. The provider-level limit acts as a ceiling — even if per-model slots are available, the provider pool must also have capacity.

### Concurrency configuration

- New field on `ProviderState`: `concurrency` (integer, default: `1`)
- Stored in `providers.json`: `"concurrency": 3`
- Exposed in admin UI: integer input in provider edit form
- **`0` = unlimited** (no throttling, all calls proceed immediately)
- **`1` = serial** (one call at a time, default for rate-limited APIs)
- **`N > 1` = parallel** (up to N concurrent calls)

### Flow

```
Chat message arrives
  → Job enqueued in Cognition lane
    → Acquire slot for provider
      → If slots available: proceed immediately
      → If at capacity: wait on condition variable
    → Execute chain (ChainRunner::ExecuteStreamingOnSession)
    → Slot released (RAII guard destructor)
      → Wakes next queued job if any
```

### Thread safety

- Jobs run on the job system's thread pool — multiple jobs can be concurrent
- `ProviderThrottle` uses a single mutex + per-provider condition variables
- Waiting jobs block their thread (standard condvar wait), not busy-spin
- The RAII guard ensures slots are released even on exceptions or early returns

### Edge cases

- **WebSocket disconnect while queued**: The job still holds its place in the queue. When it wakes and executes, the websocket send will be a no-op (connection closed). No special handling needed — the guard still releases the slot.
- **Provider deleted while jobs queued**: The throttle pool is lazily created on first `Acquire`. If the provider is removed from config, existing queued jobs will still reference the old providerId but the throttle doesn't validate against config — it just manages the counter.
- **Concurrency config changed at runtime**: The throttle reads concurrency on each `Acquire` via the lookup function. Changing the config takes effect for the *next* acquisition. Already-queued jobs use the old limit. This is acceptable — runtime changes are rare.

## Scope

### This ticket covers:
1. `ProviderThrottle` class + `SlotGuard` RAII
2. `concurrency` field on `ProviderState` (backend + persistence)
3. Admin UI: concurrency input in provider form
4. Wire throttle into websocket chat handler (Cognition lane jobs)
5. Throttle bypass when concurrency = 0

### Future tickets:
- Model entity (first-class model configs per provider)
- Per-model concurrency limits (layered on provider throttle)
- Throttle status in admin UI (active count / queue depth per provider)
- Observation stream / consolidation pipeline throttling

## Acceptance Criteria

- [ ] `ProviderThrottle` with `Acquire` / `SlotGuard` RAII implemented
- [ ] `concurrency` field persisted in `providers.json`
- [ ] Admin UI exposes concurrency in provider edit form
- [ ] Concurrency = 0 means unlimited (no throttling)
- [ ] Concurrency = 1 serializes all calls to that provider
- [ ] Concurrency = N allows up to N parallel calls, excess queued FIFO
- [ ] Slots released on success, error, and exception paths
- [ ] Queued jobs wake promptly when a slot frees
- [ ] Multiple providers throttled independently
- [ ] Existing chat flow works unchanged (default concurrency = 1)
- [ ] Full build + tests pass

# Ticket 126: Async Channel Restart on Config Update

**Status:** Draft
**Date:** 2026-07-22
**Author:** Melvin + Kestrel

## Problem

When a channel's config is updated via the admin API (`PATCH /api/v1/channels/{name}`), `ChannelManager::UpdateChannelConfig` performs a synchronous stop-then-start of the channel adapter inside the HTTP handler:

```cpp
if (m_running && state.enabled) {
    StopChannel(name);   // calls thread.join() — blocks
    state.config = config;
    StartChannel(state); // creates new poller thread
}
```

`StopChannel` joins the poller thread, which may be mid-HTTP-request (Bluesky, Telegram, VK, Twitter). `StartChannel` then establishes a new connection. This all runs synchronously on the Drogor worker thread handling the PATCH request.

**Symptoms:**
- Drogon logs `ERROR Sending more than 1 response for request. Ignoring later response`
- The admin UI request times out or receives a stale/duplicate response
- Concurrent admin UI requests (status polling, session listing) collide with the blocked handler
- Intermittent daemon instability shortly after saving channel config changes
- WebSocket connections to the admin UI can drop or enter crashloop behavior

**Reproduction:**
1. Open admin UI chat page (maintains WebSocket connection)
2. Navigate to Channels page, edit Bluesky adapter config, save
3. Observe Drogon errors in log within seconds of save
4. WebSocket may disconnect; admin UI shows reconnection attempts

## Design

### Approach: Move restart to async work queue

The HTTP handler should respond immediately after persisting the config. The channel restart should be scheduled as deferred work, executed outside the HTTP request lifecycle.

#### ChannelManager changes

Add a deferred restart queue to `ChannelManager`:

```cpp
// In ChannelManager.h
struct PendingRestart {
    std::string channel_name;
    ChannelState state;
};
std::mutex m_restartMutex;
std::vector<PendingRestart> m_pendingRestarts;
std::atomic<bool> m_restartThreadRunning{false};

void EnqueueRestart(const std::string& name, const ChannelState& state);
void ProcessPendingRestarts();
```

#### UpdateChannelConfig changes

Replace the synchronous restart with enqueue:

```cpp
// Before (synchronous — blocks HTTP handler):
if (m_running && state.enabled) {
    StopChannel(name);
    state.config = config;
    StartChannel(state);
}

// After (async — returns immediately):
if (m_running && state.enabled) {
    state.config = config;
    EnqueueRestart(name, state);
}
```

The restart queue is processed by a background thread (or a timer callback on the Drogon event loop) that drains pending restarts one at a time, with a small delay between stop and start to allow cleanup to complete.

#### Restart thread

Simple drain loop:

```cpp
void ChannelManager::ProcessPendingRestarts() {
    m_restartThreadRunning = true;
    while (true) {
        PendingRestart restart;
        {
            std::lock_guard<std::mutex> lock(m_restartMutex);
            if (m_pendingRestarts.empty()) break;
            restart = std::move(m_pendingRestarts.front());
            m_pendingRestarts.erase(m_pendingRestarts.begin());
        }

        // Stop the old channel instance
        StopChannel(restart.channel_name);

        // Brief pause to allow socket/file cleanup to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Start with new config
        StartChannel(restart.state);
    }
    m_restartThreadRunning = false;
}

void ChannelManager::EnqueueRestart(const std::string& name,
                                     const ChannelState& state) {
    {
        std::lock_guard<std::mutex> lock(m_restartMutex);
        m_pendingRestarts.push_back({name, state});
    }
    // Spawn worker thread if not already running
    if (!m_restartThreadRunning.exchange(true)) {
        std::thread(&ChannelManager::ProcessPendingRestarts, this).detach();
    }
}
```

### Why not just `std::async`?

Using a dedicated queue with a drain loop ensures:
- Restarts are **serialized** — no two channel restarts happen concurrently
- The stop-start sequence is **ordered** — stop completes before start begins
- A small **cooldown delay** between stop and start prevents resource contention
- Multiple rapid config saves coalesce — only the final state matters (can deduplicate by channel name)

### Coalescing

If the same channel is enqueued multiple times before the restart thread runs, only the latest config should be processed. Add deduplication in `EnqueueRestart`:

```cpp
void ChannelManager::EnqueueRestart(const std::string& name,
                                     const ChannelState& state) {
    {
        std::lock_guard<std::mutex> lock(m_restartMutex);
        // Remove any existing pending restart for this channel
        m_pendingRestarts.erase(
            std::remove_if(m_pendingRestarts.begin(), m_pendingRestarts.end(),
                [&](const PendingRestart& r) { return r.channel_name == name; }),
            m_pendingRestarts.end());
        m_pendingRestarts.push_back({name, state});
    }
    if (!m_restartThreadRunning.exchange(true)) {
        std::thread(&ChannelManager::ProcessPendingRestarts, this).detach();
    }
}
```

### Shutdown safety

On `ChannelManager::Stop()` (daemon shutdown), drain and discard pending restarts before stopping all channels:

```cpp
void ChannelManager::Stop() {
    // Clear pending restarts so the restart thread doesn't race with shutdown
    {
        std::lock_guard<std::mutex> lock(m_restartMutex);
        m_pendingRestarts.clear();
    }
    // ... existing stop logic ...
}
```

## Scope

- `include/animus_kernel/ChannelManager.h` — add restart queue members and methods
- `src/kernel/ChannelManager.cpp` — implement EnqueueRestart, ProcessPendingRestarts, update UpdateChannelConfig
- No admin UI changes needed — the API contract is unchanged (PATCH returns `{updated: true}`)
- No new endpoints

## Testing

1. Save Bluesky adapter config via admin UI while chat WebSocket is connected
2. Verify no "Sending more than 1 response" errors in daemon log
3. Verify WebSocket stays connected through the config save
4. Verify channel restart completes successfully (adapter reconnects with new config)
5. Rapid-fire multiple config saves — verify only the final state is applied
6. Verify daemon shutdown doesn't hang on pending restarts
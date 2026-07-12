# Ticket 056 — Channels Refactor: Merge Interfaces + Social

**Status:** Draft  
**Priority:** Medium  
**Created:** 2026-05-22  
**Depends on:** Ticket 055 (Telegram adapter — merged)  
**Blocks:** Future Slack, Discord, WhatsApp adapters

## Problem

The admin UI has two separate sections for configuring how the agent communicates:

- **Interfaces** (`/api/v1/interfaces`) — currently IRC only
- **Social** (`/api/v1/agents/{agentId}/social`) — Bluesky, VK, Telegram

Both do the same thing: authenticate to a platform → receive inbound messages → dispatch to agent sessions → auto-reply. But they use different storage, different dispatch paths, different runtime models, and different admin UI pages. This creates confusion for users (Telegram is a "social" instance but behaves like IRC) and duplicated wiring for developers (adding a new platform means choosing which system to plug into).

## Decision

Merge both systems into a single **Channels** concept. A channel is any communication pathway the agent speaks through — IRC, Telegram, VK, Bluesky, Mastodon, Slack, Discord, etc.

## Architecture

### Storage

**Current state:**
- Interfaces: JSON file on disk (`data/interfaces.json`), managed by `InterfaceManager`
- Social: SQLite via `AgentConfigStore`, flat key-value `social.{platform_id}.{field}`

**Target:** AgentConfigStore (SQLite) for everything. Flat key-value under `channel.{name}.*`:
```
channel.irc-libera.type = "irc"
channel.irc-libera.enabled = "true"
channel.irc-libera.config = {"host":"irc.libera.chat","port":6667,"nick":"animus",...}

channel.telegram-main.type = "telegram"
channel.telegram-main.enabled = "true"
channel.telegram-main.config = {"access_token":"...","polling.method":"long_poll",...}

channel.vk-community.type = "vk"
channel.vk-community.enabled = "true"
channel.vk-community.config = {"access_token":"...","group_id":"...",...}
```

Using JSON config blobs (like interfaces currently do) rather than flat key-per-field (like social currently does). This is cleaner — one read/write per channel instead of enumerating keys.

### Backend API

**Current endpoints:**
- `GET/POST/DELETE /api/v1/interfaces` + `/api/v1/interfaces/{name}` + enable/disable
- `GET/POST/DELETE /api/v1/agents/{agentId}/social` + `/api/v1/agents/{agentId}/social/{platform_id}/status`

**Target:**
```
GET    /api/v1/channels                           — list all channels
POST   /api/v1/channels                           — create channel
GET    /api/v1/channels/{name}                    — get channel detail (config + status)
PATCH  /api/v1/channels/{name}                    — update channel config
DELETE /api/v1/channels/{name}                    — delete channel
POST   /api/v1/channels/{name}/enable             — enable channel
POST   /api/v1/channels/{name}/disable            — disable channel
GET    /api/v1/channels/{name}/status             — connection status
```

No agent-scoping in the URL — channels are kernel-level resources (like interfaces currently are). Agent routing is per-channel config (`agent_id` field in config blob).

### Runtime Model

**Current state:**
- Interfaces: `IrcInterfaceRuntime` objects owned by `InterfaceManager`, message handling via closures in AdminServer's `SyncIrcInterfaces()` — ~120 lines of manual agent/provider/model resolution inline
- Social: `SocialEventPoller` threads with per-adapter loops, unified `DispatchCallback` lambda in AgentKernel

**Target:** `ChannelManager` owns all connector runtimes.

```
ChannelManager
├── IrcConnectorRuntime      (from IrcInterfaceRuntime)
├── TelegramPollerThread     (from SocialEventPoller::TelegramLongPollLoop)
├── VkPollerThread           (from SocialEventPoller::LongPollLoop)
└── (future: SlackWebSocket, DiscordGateway, etc.)
```

Each connector type implements a common interface:
```cpp
class ChannelConnector {
public:
    virtual ~ChannelConnector() = default;
    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool IsRunning() const = 0;
    virtual bool Send(const ReplyTarget& target, const std::string& text) = 0;
};
```

### Dispatch Path

**Current state:**
- IRC: AdminServer callback → manual SessionKey construction → manual agent/provider resolution → ChainRunner::ExecuteOnSession → InterfaceManager.SendPrivmsg
- Social: SocialEventPoller → DispatchCallback → AgentKernel lambda → agent resolution → chain execution → SendAutoReply

**Target:** One unified dispatch path:
```
ChannelConnector (inbound event)
  → ChannelManager::Dispatch(channelName, routingKey, message, ReplyTarget)
    → AgentKernel dispatch lambda (existing social-style)
      → resolve agent → resolve provider → ChainRunner::ExecuteOnSession
        → on success: ChannelManager::SendReply(ReplyTarget, response)
          → ChannelConnector::Send() (adapter-specific)
```

This is essentially the social dispatch path generalized. The IRC callback code in AdminServer (120 lines of manual resolution) gets deleted.

### Session Routing

**Current state:**
- IRC: `SessionKey{"irc:" + interfaceName, conversationId, threadId}` — constructed manually
- Social: `SessionKey{sessionType, platformId + ":" + routingValue, ""}` — constructed by SocialEventPoller
- Social also maintains `SocialEventRouter` for peer/post → session mapping

**Target:** Channel routing table in `ChannelManager`:
```
channel_name + routing_key → session_key
```
Unified routing with the same pattern. IRC conversations route by `channel:target` or `dm:nick`. Telegram routes by `peer:chat_id`. VK routes by `peer:peer_id` or `post:post_id`. All stored in one map.

### Admin UI

**Current state:** Two separate views — `InterfacesView.vue` and `SocialView.vue`

**Target:** One `ChannelsView.vue`:
- Table: name | type | identity | endpoint | status | connected | actions
- Create dialog: name + type dropdown (IRC, Telegram, VK, Bluesky, Mastodon) + type-specific form fields
- Type-specific fields:
  - **IRC:** host, port, server password, nick, username, realname, channels, DM settings, reconnect
  - **Telegram:** bot token
  - **VK:** group ID, access token
  - **Bluesky:** handle, app password, PDS URL
  - **Mastodon:** handle, instance URL

### Naming Sweep

| Current | Target |
|---------|--------|
| `InterfaceManager` | `ChannelManager` |
| `InterfaceState` | `ChannelState` |
| `SocialEventPoller` | Absorbed into `ChannelManager` (poller threads) |
| `SocialEventRouter` | `ChannelRouter` (part of `ChannelManager`) |
| `IrcInterfaceRuntime` | `IrcConnector` |
| `/api/v1/interfaces` | `/api/v1/channels` |
| `/api/v1/agents/{id}/social` | Removed (merged into `/api/v1/channels`) |
| `InterfacesView.vue` | `ChannelsView.vue` |
| `SocialView.vue` | Removed (merged into `ChannelsView.vue`) |
| Navigation: "Interfaces" | Navigation: "Channels" |
| Navigation: "Social" | Removed |
| Config prefix: `social.*` | Config prefix: `channel.*` |
| Config file: `interfaces.json` | Removed (SQLite only) |

## Migration Path

1. **Phase 1 — Rename + rewire** (this ticket)
   - Create `ChannelManager`, `ChannelState`, `ChannelRouter`
   - Port IRC from InterfaceManager → ChannelManager (new `IrcConnector` wrapping existing IRC socket code)
   - Port VK/Telegram/Bluesky from SocialEventPoller → ChannelManager poller threads
   - New `/api/v1/channels` routes
   - New `ChannelsView.vue` merging both admin UIs
   - Delete old InterfaceManager, SocialEventPoller, old routes, old views
   - Data migration: read `interfaces.json` → write to AgentConfigStore under `channel.*`

2. **Phase 2 — Connector interface** (follow-up)
   - Extract `ChannelConnector` abstract base
   - Each connector type becomes a self-contained class
   - Dynamic connector registration (plugins can add new channel types)

3. **Phase 3 — Module system** (future)
   - Connectors loadable as shared libraries (like current module system)
   - `connector.telegram.so`, `connector.slack.so`, etc.

## Files Changed (Phase 1 estimate)

### New
- `include/animus_kernel/ChannelManager.h`
- `src/kernel/ChannelManager.cpp`
- `admin-ui/src/views/ChannelsView.vue`

### Major rewrite
- `src/kernel/admin/AdminServer.cpp` — remove SyncIrcInterfaces callback, replace with ChannelManager wiring

### Delete
- `include/animus_kernel/admin/InterfaceManager.h`
- `src/kernel/admin/InterfaceManager.cpp`
- `include/animus_kernel/admin/InterfaceState.h`
- `include/animus_kernel/social/SocialEventPoller.h`
- `include/animus_kernel/social/SocialEventRouter.h` (if separate file)
- `src/kernel/social/SocialEventPoller.cpp`
- `src/kernel/social/SocialEventRouter.cpp` (if separate file)
- `admin-ui/src/views/InterfacesView.vue`
- `admin-ui/src/views/SocialView.vue`
- `src/kernel/admin/internal/AdminServerRoutesInterfacesSessionsMemory.inc` (IRC parts)
- `src/kernel/admin/internal/AdminServerRoutesSocial.inc`

### Modify
- `AgentKernel.cpp` — replace InterfaceManager + SocialEventPoller with ChannelManager
- `AdminServer.h` — replace member pointers
- `KernelConfig.h` — update interface config storage references
- `CMakeLists.txt` — swap source files
- Navigation/sidebar — one "Channels" entry instead of two
- i18n strings — merged + renamed

## Open Questions

1. **IRC config migration:** Auto-migrate from `interfaces.json` on first boot? Or require manual re-entry?
   - Recommendation: auto-migrate. Read the JSON file, write to AgentConfigStore, rename the file to `interfaces.json.migrated`.

2. **Social config migration:** Existing `social.*` keys in AgentConfigStore — rename to `channel.*` on boot?
   - Recommendation: yes, one-time migration in `ChannelManager::Initialize()`.

3. **Agent scoping:** Social instances were agent-scoped (`/api/v1/agents/{agentId}/social`). IRC interfaces were kernel-scoped (`/api/v1/interfaces`). Channels should be kernel-scoped (like interfaces) with an optional `agent_id` in the config blob.
   - This matches how IRC already works (config has `agent_id` field).

4. **Connector naming in code:** `ChannelConnector` as the abstract base? Or something else?
